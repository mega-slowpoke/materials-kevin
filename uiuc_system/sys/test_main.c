// test.c - KTFS Test Program
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-Identifier: NCSA
//

#include "conf.h"
#include "heap.h"
#include "console.h"
#include "elf.h"
#include "assert.h"
#include "thread.h"
#include "fs.h"
#include "io.h"
#include "device.h"
#include "dev/rtc.h"
#include "dev/uart.h"
#include "intr.h"
#include "dev/virtio.h"

#define HEAP_END 0x88000000
#define VIRTIO_MMIO_STEP (VIRTIO1_MMIO_BASE-VIRTIO0_MMIO_BASE)
extern char _kimg_blob_end[];
extern char _kimg_blob_start[];
extern char _kimg_end[];

long ktfs_readat(struct io *io, unsigned long long pos, void *buf, long len);
int ktfs_cntl(struct io *io, int cmd, void *arg);
void ktfs_close(struct io *io);
int ktfs_create(const char *name);
int ktfs_delete(const char *name);
long ktfs_writeat(struct io *io, unsigned long long pos, const void *buf, long len);

void main(void) {
    struct io *mem_io;
    struct io *termio;
    struct io *hello_io;
    int result;
    char buf[512];
    unsigned long long file_size;

    // Initialize systems
    console_init();
    devmgr_init();
    intrmgr_init();
    thrmgr_init();
    heap_init(_kimg_end, UMEM_START);

    // Attach devices
    uart_attach((void*)UART0_MMIO_BASE, UART0_INTR_SRCNO+0);
    uart_attach((void*)UART1_MMIO_BASE, UART0_INTR_SRCNO+1);
    rtc_attach((void*)RTC_MMIO_BASE);
    
    // Attach virtio devices
    for (int i = 0; i < 8; i++) {
        virtio_attach((void*)VIRTIO0_MMIO_BASE + i*VIRTIO_MMIO_STEP, VIRTIO0_INTR_SRCNO + i);
    }

    // Setup memory IO for filesystem
    size_t blob_size = _kimg_blob_end - _kimg_blob_start;
    kprintf("Filesystem blob size: %lu bytes\n", blob_size);

    // Create memory IO and mount filesystem
    mem_io = create_memory_io((void*)_kimg_blob_start, blob_size);
    result = fsmount(mem_io);
    if (result < 0) {
        kprintf("Mount FAILED: %d\n", result);
        panic("Failed to mount KTFS filesystem");
    }
    kprintf("Filesystem mounted successfully\n");

    // Open terminal device
    result = open_device("uart", 1, &termio);
    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open UART\n");
    }

    // Try to open and read "hello" file
    result = fsopen("hello", &hello_io);
    if (result < 0) {
        kprintf("Error opening hello: %d\n", result);
        panic("Failed to open hello file\n");
    }

    // Get file size
    result = ioctl(hello_io, IOCTL_GETEND, &file_size);
    if (result < 0) {
        kprintf("Error getting file size: %d\n", result);
        panic("Failed to get file size\n");
    }
    kprintf("File 'hello' size: %llu bytes\n", file_size);

    // Read file contents
    result = ioread(hello_io, buf, sizeof(buf));
    if (result < 0) {
        kprintf("Error reading file: %d\n", result);
        panic("Failed to read file\n");
    }
    kprintf("Read %d bytes from 'hello'\n", result);
    kprintf("Contents: %.*s\n", result, buf);

    // Close file
    ioclose(hello_io);
    kprintf("Test completed successfully\n");
}