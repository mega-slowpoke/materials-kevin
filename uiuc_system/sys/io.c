// io.c - Unified I/O object
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "io.h"
#include "ioimpl.h"
#include "assert.h"
#include "string.h"
#include "heap.h"
#include "error.h"
#include "thread.h"
#include "memory.h"
#include "cache.h"

#include <stddef.h>
#include <limits.h>
// INTERNAL TYPE DEFINITIONS
//

struct memio {
    struct io io; // I/O struct of memory I/O
    void * buf; // Block of memory
    size_t size; // Size of memory block
};

// #define PIPE_BUFSZ PAGE_SIZE

struct seekio {
    struct io io; // I/O struct of seek I/O
    struct io * bkgio; // Backing I/O supporting _readat_ and _writeat_
    unsigned long long pos; // Current position within backing endpoint
    unsigned long long end; // End position in backing endpoint
    int blksz; // Block size of backing endpoint
};

// INTERNAL FUNCTION DEFINITIONS
//

static int memio_cntl(struct io * io, int cmd, void * arg);

static long memio_readat (
    struct io * io, unsigned long long pos, void * buf, long bufsz);

static long memio_writeat (
    struct io * io, unsigned long long pos, const void * buf, long len);

static void seekio_close(struct io * io);

static int seekio_cntl(struct io * io, int cmd, void * arg);

static long seekio_read(struct io * io, void * buf, long bufsz);

static long seekio_write(struct io * io, const void * buf, long len);

static long seekio_readat (
    struct io * io, unsigned long long pos, void * buf, long bufsz);

static long seekio_writeat (
    struct io * io, unsigned long long pos, const void * buf, long len);


// INTERNAL GLOBAL CONSTANTS
static const struct iointf memio_iointf = {
    .close = NULL, // No special close function needed, just free memory
    .cntl = &memio_cntl,
    .read = NULL, // Does not directly support sequential reading
    .write = NULL, // Does not directly support sequential writing
    .readat = &memio_readat,
    .writeat = &memio_writeat
};
static const struct iointf seekio_iointf = {
    .close = &seekio_close,
    .cntl = &seekio_cntl,
    .read = &seekio_read,
    .write = &seekio_write,
    .readat = &seekio_readat,
    .writeat = &seekio_writeat
};

// EXPORTED FUNCTION DEFINITIONS
//

struct io * ioinit0(struct io * io, const struct iointf * intf) {
    assert (io != NULL);
    assert (intf != NULL);
    io->intf = intf;
    io->refcnt = 0;
    return io;
}

struct io * ioinit1(struct io * io, const struct iointf * intf) {
    assert (io != NULL);
    io->intf = intf;
    io->refcnt = 1;
    return io;
}

unsigned long iorefcnt(const struct io * io) {
    assert (io != NULL);
    return io->refcnt;
}

struct io * ioaddref(struct io * io) {
    assert (io != NULL);
    io->refcnt += 1;
    return io;
}

void ioclose(struct io * io) {
    assert (io != NULL);
    assert (io->intf != NULL);
    
    assert (io->refcnt != 0);
    io->refcnt -= 1;

    if (io->refcnt == 0 && io->intf->close != NULL)
        io->intf->close(io);
}

long ioread(struct io * io, void * buf, long bufsz) {
    assert (io != NULL);
    assert (io->intf != NULL);

    if (io->intf->read == NULL)
        return -ENOTSUP;
    
    if (bufsz < 0)
        return -EINVAL;
    
    return io->intf->read(io, buf, bufsz);
}

long iofill(struct io * io, void * buf, long bufsz) {
	long bufpos = 0; // position in buffer for next read
    long nread; // result of last read

    assert (io != NULL);
    assert (io->intf != NULL);

    if (io->intf->read == NULL)
        return -ENOTSUP;

    if (bufsz < 0)
        return -EINVAL;

    while (bufpos < bufsz) {
        nread = io->intf->read(io, buf+bufpos, bufsz-bufpos);
        
        if (nread <= 0)
            return (nread < 0) ? nread : bufpos;
        
        bufpos += nread;
    }

    return bufpos;
}

long iowrite(struct io * io, const void * buf, long len) {
	long bufpos = 0; // position in buffer for next write
    long n; // result of last write

    assert (io != NULL);
    assert (io->intf != NULL);
    
    if (io->intf->write == NULL)
        return -ENOTSUP;

    if (len < 0)
        return -EINVAL;
    
    do {
        n = io->intf->write(io, buf+bufpos, len-bufpos);

        if (n <= 0)
            return (n < 0) ? n : bufpos;

        bufpos += n;
    } while (bufpos < len);

    return bufpos;
}

long ioreadat (
    struct io * io, unsigned long long pos, void * buf, long bufsz)
{
    assert (io != NULL);
    assert (io->intf != NULL);
    
    if (io->intf->readat == NULL)
        return -ENOTSUP;
    
    if (bufsz < 0)
        return -EINVAL;
    
    return io->intf->readat(io, pos, buf, bufsz);
}

long iowriteat (
    struct io * io, unsigned long long pos, const void * buf, long len)
{
    assert (io != NULL);
    assert (io->intf != NULL);
    
    if (io->intf->writeat == NULL)
        return -ENOTSUP;
    
    if (len < 0)
        return -EINVAL;
    
    return io->intf->writeat(io, pos, buf, len);
}

int ioblksz(struct io * io) {
    int blksz = 0;
    int result = ioctl(io, IOCTL_GETBLKSZ, &blksz);
    if (result != 0) {
        return 512; // Default block size if ioctl fails (matches KTFS_BLKSZ)
    }
    return blksz > 0 ? blksz : 512; // Ensure a valid block size
}

int ioctl(struct io * io, int cmd, void * arg) {
    kprintf("ioctl: ENTER - io=%p, cmd=%d, arg=%p\n", io, cmd, arg);
    assert (io != NULL);
    assert (io->intf != NULL);

    if (io->intf->cntl != NULL) {
        kprintf("ioctl: Calling interface cntl function at %p\n", io->intf->cntl);
        int result = io->intf->cntl(io, cmd, arg);
        kprintf("ioctl: Result from cntl = %d\n", result);
        return result;
    } else if (cmd == IOCTL_GETBLKSZ) {
        kprintf("ioctl: Handling IOCTL_GETBLKSZ directly\n");
        if (arg != NULL) {
            *(int *)arg = 512; // Default block size
        }
        return 0; // Success
    } else {
        kprintf("ioctl: Command %d not supported\n", cmd);
        return -ENOTSUP;
    }
}

int ioseek(struct io * io, unsigned long long pos) {
    return ioctl(io, IOCTL_SETPOS, &pos);
}

struct io * create_memory_io(void * buf, size_t size) {
    struct memio * mio;

// Validate parameters
    if (buf == NULL || size == 0) {
        return NULL;
    }

// Allocate memio structure
    mio = kmalloc(sizeof(struct memio));
    if (mio == NULL) {
        return NULL;
    }

// Initialize memio structure
    mio->buf = buf;
    mio->size = size;

// Initialize io structure and return
    return ioinit1(&mio->io, &memio_iointf);
    // FIX ME
}

struct io * create_seekable_io(struct io * io) {
    struct seekio * sio;
    unsigned long end;
    int result;
    int blksz;
    
    blksz = ioblksz(io);
    assert (0 < blksz);
    
    // block size must be power of two
    assert ((blksz & (blksz - 1)) == 0);

    result = ioctl(io, IOCTL_GETEND, &end);
    assert (result == 0);
    
    sio = kcalloc(1, sizeof(struct seekio));

    sio->pos = 0;
    sio->end = end;
    sio->blksz = blksz;
    sio->bkgio = ioaddref(io);

    return ioinit1(&sio->io, &seekio_iointf);

};

// INTERNAL FUNCTION DEFINITIONS
//

long memio_readat (
    struct io * io,
    unsigned long long pos,
    void * buf, long bufsz)
{
    // FIX ME
    struct memio * const mio = (void*)io - offsetof(struct memio, io);

// Validate parameters
    if (bufsz < 0) {
        return -EINVAL;
    }

// Check if position is out of range
    if (pos >= mio->size) {
        return 0; // Position is beyond end, no data to read
    }

// Limit read size to not exceed buffer end
    if (pos + bufsz > mio->size) {
        bufsz = mio->size - pos;
    }

// Copy data to user buffer
    memcpy(buf, (char*)mio->buf + pos, bufsz);

    return bufsz;
}

long memio_writeat (
    struct io * io,
    unsigned long long pos,
    const void * buf, long len)
{
    struct memio * const mio = (void*)io - offsetof(struct memio, io);

// Validate parameters
    if (len < 0) {
        return -EINVAL;
    }

// Check if position is out of range
    if (pos >= mio->size) {
        return 0; // Position is beyond end, cannot write
    }

// Limit write size to not exceed buffer end
    if (pos + len > mio->size) {
        len = mio->size - pos;
    }

// Copy data from user buffer
    memcpy((char*)mio->buf + pos, buf, len);

    return len;
    // FIX ME
}

int memio_cntl(struct io * io, int cmd, void * arg) {
    
    // FIX ME
    struct memio * const mio = (void*)io - offsetof(struct memio, io);
    unsigned long long * ullarg = arg;

    switch (cmd) {
        case IOCTL_GETBLKSZ:
            if(arg !=NULL){
                *(int*)arg = CACHE_BLKSZ;
            }
            return 1;

        case IOCTL_GETEND:
            if (ullarg == NULL) {
                return -EINVAL;
            }
            *ullarg = mio->size;
            return 0;

        case IOCTL_SETEND:
            if (ullarg == NULL) {
                return -EINVAL;
            }
            if (*ullarg > mio->size) {
                return -EINVAL;
            }
            mio->size = *ullarg;
            return 0;
        default:
            return -ENOTSUP;
        }
}

void seekio_close(struct io * io) {
    struct seekio * const sio = (void*)io - offsetof(struct seekio, io);
    ioclose(sio->bkgio);
    kfree(sio);
}

int seekio_cntl(struct io * io, int cmd, void * arg) {
    struct seekio * const sio = (void*)io - offsetof(struct seekio, io);
    unsigned long long * ullarg = arg;
    int result;

    switch (cmd) {
    case IOCTL_GETBLKSZ:
        return sio->blksz;
    case IOCTL_GETPOS:
        *ullarg = sio->pos;
        return 0;
    case IOCTL_SETPOS:
        // New position must be multiple of block size
        if ((*ullarg & (sio->blksz - 1)) != 0)
            return -EINVAL;
        
        // New position must not be past end
        if (*ullarg > sio->end)
            return -EINVAL;
        
        sio->pos = *ullarg;
        return 0;
    case IOCTL_GETEND:
        *ullarg = sio->end;
        return 0;
    case IOCTL_SETEND:
        // Call backing endpoint ioctl and save result
        result = ioctl(sio->bkgio, IOCTL_SETEND, ullarg);
        if (result == 0)
            sio->end = *ullarg;
        return result;
    default:
        return ioctl(sio->bkgio, cmd, arg);
    }
}

long seekio_read(struct io * io, void * buf, long bufsz) {
    struct seekio * const sio = (void*)io - offsetof(struct seekio, io);
    unsigned long long const pos = sio->pos;
    unsigned long long const end = sio->end;
    long rcnt;

    // Cannot read past end
    if (end - pos < bufsz)
        bufsz = end - pos;

    if (bufsz == 0)
        return 0;
        
    // Request must be for at least blksz bytes if not zero
    if (bufsz < sio->blksz)
        return -EINVAL;

    // Truncate buffer size to multiple of blksz
    bufsz &= ~(sio->blksz - 1);

    rcnt = ioreadat(sio->bkgio, pos, buf, bufsz);
    sio->pos = pos + (rcnt < 0) ? 0 : rcnt;
    return rcnt;
}


long seekio_write(struct io * io, const void * buf, long len) {
    struct seekio * const sio = (void*)io - offsetof(struct seekio, io);
    unsigned long long const pos = sio->pos;
    unsigned long long end = sio->end;
    int result;
    long wcnt;

    if (len == 0)
        return 0;
    
    // Request must be for at least blksz bytes
    if (len < sio->blksz)
        return -EINVAL;
    
    // Truncate length to multiple of blksz
    len &= ~(sio->blksz - 1);

    // Check if write is past end. If it is, we need to change end position.

    if (end - pos < len) {
        if (ULLONG_MAX - pos < len)
            return -EINVAL;
        
        end = pos + len;

        result = ioctl(sio->bkgio, IOCTL_SETEND, &end);
        
        if (result != 0)
            return result;
        
        sio->end = end;
    }

    wcnt = iowriteat(sio->bkgio, sio->pos, buf, len);
    sio->pos = pos + (wcnt < 0) ? 0 : wcnt;
    return wcnt;
}

long seekio_readat (
    struct io * io, unsigned long long pos, void * buf, long bufsz)
{
    struct seekio * const sio = (void*)io - offsetof(struct seekio, io);
    return ioreadat(sio->bkgio, pos, buf, bufsz);
}

long seekio_writeat (
    struct io * io, unsigned long long pos, const void * buf, long len)
{
    struct seekio * const sio = (void*)io - offsetof(struct seekio, io);
    return iowriteat(sio->bkgio, pos, buf, len);
}