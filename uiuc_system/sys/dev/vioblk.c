// vioblk.c - VirtIO serial port (console)
// 
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef VIOBLK_TRACE
#define TRACE
#endif

#ifdef VIOBLK_DEBUG
#define DEBUG
#endif

#include "virtio.h"
#include "intr.h"
#include "assert.h"
#include "heap.h"
#include "io.h"
#include "device.h"
#include "thread.h"
#include "error.h"
#include "string.h"
#include "assert.h"
#include "ioimpl.h"
#include "io.h"
#include "conf.h"
#include "errno.h"
#include <limits.h>

// COMPILE-TIME PARAMETERS
//

#ifndef VIOBLK_INTR_PRIO
#define VIOBLK_INTR_PRIO 1
#endif

#ifndef VIOBLK_NAME
#define VIOBLK_NAME "vioblk"
#endif

// INTERNAL CONSTANT DEFINITIONS
//

// VirtIO block device feature bits (number, *not* mask)

#define VIRTIO_BLK_F_SIZE_MAX       1
#define VIRTIO_BLK_F_SEG_MAX        2
#define VIRTIO_BLK_F_GEOMETRY       4
#define VIRTIO_BLK_F_RO             5
#define VIRTIO_BLK_F_BLK_SIZE       6
#define VIRTIO_BLK_F_FLUSH          9
#define VIRTIO_BLK_F_TOPOLOGY       10
#define VIRTIO_BLK_F_CONFIG_WCE     11
#define VIRTIO_BLK_F_MQ             12
#define VIRTIO_BLK_F_DISCARD        13
#define VIRTIO_BLK_F_WRITE_ZEROES   14

// INTERNAL FUNCTION DECLARATIONS
//
static int vioblk_open(struct io ** ioptr, void * aux);
static void vioblk_close(struct io * io);

static long vioblk_readat(
    struct io * io,
    unsigned long long pos,
    void * buf,
    long bufsz);

static long vioblk_writeat(
    struct io * io,
    unsigned long long pos,
    const void * buf,
    long len);

static int vioblk_cntl(
    struct io * io, int cmd, void * arg);

static void vioblk_isr(int srcno, void * aux);

// EXPORTED FUNCTION DEFINITIONS
//

// Global lock
struct lock vioblk_lock;

// VirtIO block request structure (section 5.2.6 of VirtIO spec)
struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

// Request types
#define VIRTIO_BLK_T_IN           0  // Read
#define VIRTIO_BLK_T_OUT          1  // Write
#define VIRTIO_BLK_T_FLUSH        4  
#define VIRTIO_BLK_T_GET_ID       8 
#define VIRTIO_BLK_T_GET_LIFETIME 10 
#define VIRTIO_BLK_T_DISCARD      11 
#define VIRTIO_BLK_T_WRITE_ZEROES 13 
#define VIRTIO_BLK_T_SECURE_ERASE 14

// Status codes
#define VIRTIO_BLK_S_OK        0  // Success
#define VIRTIO_BLK_S_IOERR     1  // Device or driver error
#define VIRTIO_BLK_S_UNSUPP    2  // Request is unsupported

// Interrupt status bits
#define VIRTQ_NOTIFICATION_BIT 0x01
#define VIRTQ_CONFIGURATION_BIT 0x02

// VirtIO block device structure
struct vioblk_device {
    volatile struct virtio_mmio_regs* regs;   // Pointer to MMIO registers
    struct iointf iointf;
    uint16_t instno;
    uint16_t irqno;

    struct io io;
    uint32_t blksz;    // Size of each block
    uint64_t pos;      // Current position
    uint64_t size;     // Total size of the device in bytes
    uint64_t blkcnt;   // Total number of blocks in the device
    
    struct {
        uint16_t last_used_idx; 
        struct condition used_updated; // Notification from ISR
        union {
            struct virtq_avail avail;
            char _avail_filler[VIRTQ_AVAIL_SIZE(1)];
        }; 
        union {
            volatile struct virtq_used used;
            char _used_filler[VIRTQ_USED_SIZE(1)];
        };
        struct virtq_desc desc[4];
        struct virtio_blk_req req_header;
        uint8_t req_status;
    } vq;

    uint64_t blkno;    // Block number in the buffer
    char* blkbuf;      // Pointer to the block buffer
};

void vioblk_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    regs->status |= VIRTIO_STAT_DRIVER;
    __sync_synchronize();

    virtio_featset_t needed_features, wanted_features, enabled_features;
    uint32_t blksz;
    struct vioblk_device* vioblk;

    virtio_featset_init(needed_features);
    virtio_featset_add(needed_features, VIRTIO_F_RING_RESET);
    virtio_featset_add(needed_features, VIRTIO_F_INDIRECT_DESC);
    virtio_featset_init(wanted_features);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_TOPOLOGY);

    int result = virtio_negotiate_features(regs, enabled_features, wanted_features, needed_features);
    if (result != 0) {
        kprintf("%p: virtio feature negotiation failed\n", regs);
        return;
    }

    if (virtio_featset_test(enabled_features, VIRTIO_BLK_F_BLK_SIZE)) {
        blksz = regs->config.blk.blk_size;
    } else {
        blksz = 512;
    }

    assert(((blksz - 1) & blksz) == 0);

    static const struct iointf vioblk_iointf = {
        .close = vioblk_close,
        .cntl = vioblk_cntl,
        .readat = vioblk_readat,
        .writeat = vioblk_writeat
    };

    vioblk = kmalloc(sizeof(struct vioblk_device) + blksz);
    memset(vioblk, 0, sizeof(struct vioblk_device));

    vioblk->blkbuf = (char*)(vioblk + 1);

    ioinit0(&vioblk->io, &vioblk_iointf);
    vioblk->regs = regs;
    vioblk->iointf = vioblk_iointf;
    vioblk->irqno = irqno;
    vioblk->blksz = blksz;
    vioblk->pos = 0;

    vioblk->size = regs->config.blk.capacity * vioblk->blksz;
    vioblk->blkcnt = regs->config.blk.capacity;
    vioblk -> blkno = 0;
    // Initialize virtqueue descriptors
    vioblk->vq.desc[0].addr = (uint64_t)&vioblk->vq.desc[1];
    vioblk->vq.desc[0].len = 3 * sizeof(vioblk->vq.desc);
    vioblk->vq.desc[0].flags = VIRTQ_DESC_F_INDIRECT;
    vioblk->vq.desc[0].next = 0;

    vioblk->vq.desc[1].addr = (uint64_t)&vioblk->vq.req_header;
    vioblk->vq.desc[1].len = sizeof(vioblk->vq.req_header);
    vioblk->vq.desc[1].flags = VIRTQ_DESC_F_NEXT;
    vioblk->vq.desc[1].next = 1;

    vioblk->vq.desc[2].addr = (uint64_t)vioblk->blkbuf;
    vioblk->vq.desc[2].len = blksz;
    vioblk->vq.desc[2].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
    vioblk->vq.desc[2].next = 2;

    vioblk->vq.desc[3].addr = (uint64_t)&vioblk->vq.req_status;
    vioblk->vq.desc[3].len = sizeof(vioblk->vq.req_status);
    vioblk->vq.desc[3].flags = VIRTQ_DESC_F_WRITE;
    vioblk->vq.desc[3].next = 0;

    lock_init(&vioblk_lock);
    enable_intr_source(irqno, VIOBLK_INTR_PRIO, &vioblk_isr, vioblk);
    virtio_attach_virtq(regs, 0, 1, (uint64_t)(&vioblk->vq.desc), (uint64_t)(&vioblk->vq.used), (uint64_t)(&vioblk->vq.avail));
    virtio_enable_virtq(regs, 0);
    vioblk->instno = register_device("vioblk", &vioblk_open, vioblk);
    condition_init(&vioblk->vq.used_updated, "used_update");

    regs->status |= VIRTIO_STAT_DRIVER_OK;
    __sync_synchronize();
}

int vioblk_open(struct io** ioptr, void* aux) {
    struct vioblk_device* vioblk = aux;
    if (vioblk->io.refcnt > 0) {
        return -EBUSY;
    }
    vioblk->vq.last_used_idx = vioblk->vq.used.idx;

    vioblk->vq.avail.ring[vioblk->vq.avail.idx % VIRTQ_LEN_MAX] = 0;
    vioblk->vq.avail.idx++;
    virtio_notify_avail(vioblk->regs, 0);

    vioblk->vq.avail.flags = 0;
    vioblk->vq.used.flags = 0;
    *ioptr = &vioblk->io;
    vioblk->io.refcnt++;
    return 0;
}

void vioblk_close(struct io* ioptr) {
    struct vioblk_device* vioblk = (void*)ioptr - offsetof(struct vioblk_device, io);
    vioblk->vq.avail.idx = 0;
    vioblk->vq.last_used_idx = 0;

    if (vioblk->irqno >= 0) {
        disable_intr_source(vioblk->irqno);
    }
}

long vioblk_readat(struct io * io, unsigned long long pos, void * buf, long bufsz) {
    lock_acquire(&vioblk_lock);
    struct vioblk_device* vioblk = (void*)io - offsetof(struct vioblk_device, io);
    if (!io || !buf) {
        lock_release(&vioblk_lock);
        return -EINVAL;
    }

    kprintf("vioblk_readat: pos=%llu, bufsz=%ld, blksz=%u\n", pos, bufsz, vioblk->blksz);

    // Only require position alignment, not buffer size
    if (pos % vioblk->blksz != 0) {
        kprintf("vioblk_readat: Misaligned position\n");
        lock_release(&vioblk_lock);
        return -EINVAL;
    }

    if (vioblk->size < pos + bufsz) {
        kprintf("vioblk_readat: Adjusting bufsz from %ld to %llu\n", bufsz, vioblk->size - pos);
        bufsz = vioblk->size - pos;
    }

    if (bufsz <= 0) {
        lock_release(&vioblk_lock);
        return 0;
    }

    struct virtio_blk_req* req = &vioblk->vq.req_header;
    uint64_t bytes_read = 0;

    uint64_t start_number = pos / vioblk->blksz;
    uint64_t end_number = (pos + bufsz - 1) / vioblk->blksz;

    kprintf("vioblk_readat: Reading from sector %llu to %llu\n", start_number, end_number);

    for (uint64_t i = start_number; i <= end_number; i++) {
        uint64_t bytes_copy = vioblk->blksz;
        if (i == end_number) {
            bytes_copy = bufsz - bytes_read;
        }

        req->type = VIRTIO_BLK_T_IN;
        req->sector = i;

        vioblk->vq.desc[2].addr = (uint64_t)vioblk->blkbuf;
        vioblk->vq.desc[2].len = vioblk->blksz; // Always read full block
        vioblk->vq.desc[2].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;

        vioblk->vq.avail.ring[vioblk->vq.avail.idx % VIRTQ_LEN_MAX] = 0;
        vioblk->vq.avail.idx++;

        kprintf("vioblk_readat: Issuing read request for sector %llu, bytes=%llu\n", i, bytes_copy);
        virtio_notify_avail(vioblk->regs, 0);

        int pie = disable_interrupts();
        while (vioblk->vq.used.idx != vioblk->vq.avail.idx) {
            condition_wait(&vioblk->vq.used_updated);
        }
        restore_interrupts(pie);

        kprintf("vioblk_readat: Request status for sector %llu: %d\n", i, vioblk->vq.req_status);
        if (vioblk->vq.req_status != VIRTIO_BLK_S_OK) {
            kprintf("vioblk_readat: Request failed\n");
            lock_release(&vioblk_lock);
            return -EIO;
        }

        // Print the first 32 bytes of the read data
        kprintf("vioblk_readat: First 32 bytes of read data:\n");
        for (int j = 0; j < 32 && j < bytes_copy; j++) {
            kprintf("  [%d] = 0x%02x\n", j, ((unsigned char *)vioblk->blkbuf)[j]);
        }

        // Only copy the requested number of bytes
        memcpy(buf + bytes_read, vioblk->blkbuf, bytes_copy);
        bytes_read += bytes_copy;
    }

    kprintf("vioblk_readat: Successfully read %llu bytes\n", bytes_read);
    lock_release(&vioblk_lock);
    return bytes_read;
}

long vioblk_writeat(struct io * io, unsigned long long pos, const void * buf, long len) {
    lock_acquire(&vioblk_lock);
    struct vioblk_device* vioblk = (void*)io - offsetof(struct vioblk_device, io);
    if (!io || !buf) {
        lock_release(&vioblk_lock);
        return -EINVAL;
    }

    // Check alignment
    if (pos % vioblk->blksz != 0 || len % vioblk->blksz != 0) {
        lock_release(&vioblk_lock);
        return -EINVAL;
    }

    // Check bounds
    if (vioblk->size < pos + len) {
        len = vioblk->size - pos;
    }

    if (len <= 0) {
        lock_release(&vioblk_lock);
        return 0;
    }

    struct virtio_blk_req* req = &vioblk->vq.req_header;
    uint64_t bytes_write = 0;

    uint64_t start_number = pos / vioblk->blksz;
    uint64_t end_number = (pos + len - 1) / vioblk->blksz;

    for (uint64_t i = start_number; i <= end_number; i++) {
        uint64_t bytes_copy = vioblk->blksz;
        if (i == end_number) {
            bytes_copy = len - bytes_write;
        }

        memcpy(vioblk->blkbuf, (char*)buf + bytes_write, bytes_copy);

        req->type = VIRTIO_BLK_T_OUT;
        req->sector = i;
        vioblk->vq.desc[2].addr = (uint64_t)vioblk->blkbuf;
        vioblk->vq.desc[2].len = bytes_copy;
        vioblk->vq.desc[2].flags = VIRTQ_DESC_F_NEXT;

        vioblk->vq.avail.ring[vioblk->vq.avail.idx % VIRTQ_LEN_MAX] = 0;
        vioblk->vq.avail.idx++;

        virtio_notify_avail(vioblk->regs, 0);

        int pie = disable_interrupts();
        while (vioblk->vq.used.idx != vioblk->vq.avail.idx) {
            condition_wait(&vioblk->vq.used_updated);
        }
        restore_interrupts(pie);

        if (vioblk->vq.req_status != VIRTIO_BLK_S_OK) {
            lock_release(&vioblk_lock);
            return -EIO;
        }

        bytes_write += bytes_copy;
    }

    lock_release(&vioblk_lock);
    return bytes_write;
}

int vioblk_cntl(struct io * io, int cmd, void * arg) {
    kprintf("vioblk_cntl: ENTER - io=%p, cmd=%d, arg=%p\n", io, cmd, arg);
    if (!io) {
        return -EINVAL;
    }
    struct vioblk_device* vioblk = (void*)io - offsetof(struct vioblk_device, io);
    switch (cmd) {
        case IOCTL_GETBLKSZ:
            if (!arg) {
                return -EINVAL;
            }
            *((uint32_t*)arg) = vioblk->blksz;
            return 1;
        case IOCTL_GETEND:
            if (!arg) {
                return -EINVAL;
            }
            *((uint64_t*)arg) = vioblk->size;
            return 0;
        case IOCTL_SETPOS:
            if (!arg) {
                return -EINVAL;
            }
            vioblk->pos = *((uint64_t*)arg);
            return 1;
        case IOCTL_GETPOS:
            if (!arg) {
                return -EINVAL;
            }
            *((uint64_t*)arg) = vioblk->pos;
            return 0;
        case IOCTL_SETEND:
            return -ENOTSUP;
        default:
            return -ENOTSUP;
    }
}

void vioblk_isr(int irqno, void * aux) {
    struct vioblk_device* vioblk = aux;
    uint32_t line_status = vioblk->regs->interrupt_status;
    if (line_status == 0) {
        return;
    }

    if (line_status & VIRTQ_NOTIFICATION_BIT) {
        vioblk->vq.last_used_idx = vioblk->vq.used.idx;
        condition_broadcast(&vioblk->vq.used_updated);
        vioblk->regs->interrupt_ack |= VIRTQ_NOTIFICATION_BIT;
    } else if (line_status & VIRTQ_CONFIGURATION_BIT) {
        vioblk->regs->interrupt_ack |= VIRTQ_CONFIGURATION_BIT;
    }
}