// viorng.c - VirtIO rng device
// 
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "virtio.h"
#include "intr.h"
#include "heap.h"
#include "io.h"
#include "device.h"
#include "error.h"
#include "string.h"
#include "ioimpl.h"
#include "assert.h"
#include "conf.h"
#include "intr.h"
#include "console.h"


// INTERNAL CONSTANT DEFINITIONS
//

#ifndef VIORNG_BUFSZ
#define VIORNG_BUFSZ 256
#endif

#ifndef VIORNG_NAME
#define VIORNG_NAME "rng"
#endif

#ifndef VIORNG_IRQ_PRIO
#define VIORNG_IRQ_PRIO 1
#endif

// INTERNAL TYPE DEFINITIONS
//

struct viorng_device {
    volatile struct virtio_mmio_regs * regs;
    int irqno;
    int instno;

    struct io io;

    struct {
        uint16_t last_used_idx;

        union {
            struct virtq_avail avail;
            char _avail_filler[VIRTQ_AVAIL_SIZE(1)];
        };

        union {
            volatile struct virtq_used used;
            char _used_filler[VIRTQ_USED_SIZE(1)];
        };

        struct virtq_desc desc[1];
    } vq;

    unsigned int bufcnt;
    char buf[VIORNG_BUFSZ];
};

// INTERNAL FUNCTION DECLARATIONS
//

static int viorng_open(struct io ** ioptr, void * aux);
static void viorng_close(struct io * io);
static long viorng_read(struct io * io, void * buf, long bufsz);
static void viorng_isr(int irqno, void * aux);

// EXPORTED FUNCTION DEFINITIONS
//

// void viorng_attach(volatile struct virtio_mmio_regs * regs, int irqno)
// Inputs: volatile struct virtio_mmio_regs * regs - Pointer to VirtIO MMIO registers
//         int irqno - Interrupt request number
// Outputs: None
// Description: Sets up a VirtIO RNG device and registers it.
// Side Effects: Allocates device structure, configures VirtIO queue, registers device, enables driver.

void viorng_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    //           FIXME add additional declarations here if needed
    static const struct iointf viorng_io_funcs = {
        .close = viorng_close,
        .read = viorng_read
    };

    struct viorng_device *rng_dev;

    virtio_featset_t enabled_features, wanted_features, needed_features;
    int negotiation_result;
    
    assert(regs->device_id == VIRTIO_ID_RNG);

    regs->status |= VIRTIO_STAT_DRIVER;
    __sync_synchronize();

    virtio_featset_init(needed_features);
    virtio_featset_init(wanted_features);
    negotiation_result = virtio_negotiate_features(regs, enabled_features, wanted_features, needed_features);

    if (negotiation_result != 0) {
        kprintf("%p: virtio feature negotiation failed\n", regs);
        return;
    }
    //           FIXME Finish viorng initialization here! 
    rng_dev = kcalloc(1, sizeof(struct viorng_device));     // Allocate device memory
    rng_dev->regs = regs;
    rng_dev->irqno = 12;  // hardcode 12

    ioinit0(&rng_dev->io, &viorng_io_funcs);    // Init I/O interface
    virtio_attach_virtq(regs, 0, 1, (uint64_t)&rng_dev->vq.desc[0], (uint64_t)&rng_dev->vq.used, (uint64_t)&rng_dev->vq.avail);
    rng_dev->vq.last_used_idx = 0;
    rng_dev->bufcnt = 0;        // Clear buffer count

    // Enable the virtqueue
    rng_dev->regs->queue_sel = 0;       // Select queue 0
    __sync_synchronize();
    rng_dev->regs->queue_ready = 1; //enable queue

    rng_dev->instno = register_device(VIORNG_NAME, viorng_open, rng_dev); //register device
    regs->status |= VIRTIO_STAT_DRIVER_OK;    // Driver ready
    kprintf("RNG attached at %p, IRQ %d\n", regs, rng_dev->irqno);
    __sync_synchronize();       //sync memory
}

// int viorng_open(struct io ** ioptr, void * aux)
// Inputs: struct io ** ioptr - Pointer to store I/O structure
//         void * aux - Pointer to viorng_device structure
// Outputs: int - 0 on success, -EBUSY if already open
// Description: Opens the RNG device for use.
// Side Effects: Resets queue indices, enables ISR, sets I/O pointer, increments refcount.

int viorng_open(struct io ** ioptr, void * aux) {
    //           FIXME your code here
    struct viorng_device *entropy_device = (struct viorng_device *)aux;

    if (entropy_device->io.refcnt != 0) {   // Check if alr open
        return -EBUSY;
    }

    entropy_device->vq.avail.idx = 0;       // Reset available index
    entropy_device->vq.used.idx = 0;        // reset used index

    enable_intr_source(entropy_device->irqno, VIORNG_IRQ_PRIO, viorng_isr, entropy_device);
    *ioptr = &entropy_device->io;   // set i/o pointer
    entropy_device->io.refcnt = entropy_device->io.refcnt + 1;

    kprintf("RNG opened, IRQ %d\n", entropy_device->irqno);
    return 0;
}

// void viorng_close(struct io * io)
// Inputs: struct io * io - Pointer to I/O structure
// Outputs: None
// Description: Closes the RNG device.
// Side Effects: Resets queue indices, disables ISR, clears refcount.

void viorng_close(struct io * io) {
    //           FIXME your code here
    struct viorng_device *entropy_device = (struct viorng_device *)((char *)io - offsetof(struct viorng_device, io));

    entropy_device->vq.avail.idx = 0;       //reset available index
    entropy_device->vq.used.idx = 0;    //  track bytes read
    disable_intr_source(entropy_device->irqno); //disable interrupt
    entropy_device->io.refcnt = 0;  //clear refcont
}

// long viorng_read(struct io * io, void * buf, long bufsz)
// Inputs: struct io * io - Pointer to I/O structure
//         void * buf - Buffer to store random bytes
//         long bufsz - Number of bytes to read
// Outputs: long - Number of bytes read, 0 if bufsz <= 0
// Description: Reads random bytes from the RNG into a buffer.
// Side Effects: Drains buffer if data exists, requests new data via VirtIO queue, updates buffer count.

long viorng_read(struct io * io, void * buf, long bufsz) {
    //           FIXME your code here
    struct viorng_device *entropy_device = (struct viorng_device *)((char *)io - offsetof(struct viorng_device, io));
    char *destination = (char *)buf;        //set destination buffer
    long bytes_transferred = 0; //track bytes

    if (bufsz <= 0) {
        return 0;
    }

    kprintf("Reading %ld bytes from RNG\n", bufsz); //debug

    while (bytes_transferred < bufsz && entropy_device->bufcnt > 0) { //buffer out
        destination[bytes_transferred] = entropy_device->buf[VIORNG_BUFSZ - entropy_device->bufcnt];
        entropy_device->bufcnt = entropy_device->bufcnt - 1;
        bytes_transferred = bytes_transferred + 1;
    }

    while (bytes_transferred < bufsz) { //need more data
        // buffer set up
        entropy_device->vq.desc[0].addr = (uint64_t)(entropy_device->buf); 
        entropy_device->vq.desc[0].len = VIORNG_BUFSZ;
        entropy_device->vq.desc[0].flags = VIRTQ_DESC_F_WRITE;
        entropy_device->vq.desc[0].next = 0;

        entropy_device->vq.avail.ring[0] = 0;
        entropy_device->vq.avail.idx = entropy_device->vq.avail.idx + 1;

        entropy_device->regs->queue_notify = 0; //notify device
        kprintf("Notified device, waiting\n"); //debug

        //wait for device
        while (entropy_device->vq.used.idx == entropy_device->vq.last_used_idx) {
            kprintf("Waiting: used.idx=%d, last_used_idx=%d\n", entropy_device->vq.used.idx, entropy_device->vq.last_used_idx);
        }

        //update buffer
        entropy_device->vq.last_used_idx = entropy_device->vq.used.idx;
        entropy_device->bufcnt = entropy_device->vq.used.ring[0].len;
        kprintf("Device responded, bufcnt=%d\n", entropy_device->bufcnt);

        // drain new data
        while (bytes_transferred < bufsz && entropy_device->bufcnt > 0) {
            destination[bytes_transferred] = entropy_device->buf[VIORNG_BUFSZ - entropy_device->bufcnt];
            entropy_device->bufcnt = entropy_device->bufcnt - 1;
            bytes_transferred = bytes_transferred + 1;
        }
    }

    kprintf("Read complete, returning %ld bytes\n", bytes_transferred);
    return bytes_transferred;
}

// void viorng_isr(int irqno, void * aux)
// Inputs: int irqno - Interrupt request number
//         void * aux - Pointer to viorng_device structure
// Outputs: None
// Description: Handles RNG interrupt to acknowledge data availability.
// Side Effects: Clears interrupt status, syncs used index with device.

void viorng_isr(int irqno, void * aux) {
    //           FIXME your code here
    struct viorng_device *entropy_device = (struct viorng_device *)aux;
    kprintf("RNG ISR: IRQ %d, status %d\n", irqno, entropy_device->regs->interrupt_status);
    // clear interpupt signal
    entropy_device->regs->interrupt_ack = entropy_device->regs->interrupt_status;

    // Sync index
    entropy_device->vq.last_used_idx = entropy_device->vq.used.idx;



}