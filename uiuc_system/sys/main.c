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
#include <errno.h>

#define VIRTIO_MMIO_STEP (VIRTIO1_MMIO_BASE-VIRTIO0_MMIO_BASE)

extern char _kimg_end[]; 

void run_trek(void *arg) {
    struct io *termio = (struct io *)arg;
    void (*exe_entry)(void);
    struct io *trekio;
    int result;
    
    kprintf("Trek thread started\n");
    
    result = fsopen("trek", &trekio);
    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open trek in thread\n");
    }
    
    result = elf_load(trekio, &exe_entry);
    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to load trek ELF\n");
    }
    
    kprintf("Trek program loaded, starting execution\n");
    
    exe_entry();
    
    kprintf("Trek program returned - should not happen\n");
    thread_exit();
}

void main(void) {
    struct io *blkio;
    struct io *termio;
    struct io *trekio;
    struct io *hello_io;
    int result;
    int i;
    int tid;
    void (*exe_entry)(void);
    
    console_init();
    devmgr_init();
    intrmgr_init();
    thrmgr_init();
    heap_init(UMEM_START, (void*)(UMEM_START + 16 * 1024 * 1024));
    
    uart_attach((void*)UART0_MMIO_BASE, UART0_INTR_SRCNO+0);
    uart_attach((void*)UART1_MMIO_BASE, UART0_INTR_SRCNO+1);
    rtc_attach((void*)RTC_MMIO_BASE);
    
    for (i = 0; i < 8; i++) {
        virtio_attach((void*)(VIRTIO0_MMIO_BASE + i*VIRTIO_MMIO_STEP), VIRTIO0_INTR_SRCNO + i);
    }
    
    result = open_device("vioblk", 0, &blkio);
    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open vioblk\n");
    }
    
    // Mount filesystem
    if (fsmount(blkio) < 0) {
        kprintf("Failed to mount filesystem\n");
        return;
    }
    kprintf("Filesystem mounted successfully\n");

    // Open hello file
    result = fsopen("hello", &hello_io);
    if (result < 0) {
        kprintf("Failed to open hello file: %d\n", result);
        return;
    }
    kprintf("Successfully opened hello file\n");

    // Open trek file
    result = fsopen("trek", &trekio);
    if (result < 0) {
        kprintf("Failed to open trek file: %d\n", result);
        return;
    }
    kprintf("Successfully opened trek file\n");
    
    result = open_device("uart", 1, &termio);
    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open UART\n");
    }
    
    kprintf("\n===== TEST 1: Running Trek in main thread =====\n");

    result = elf_load(trekio, &exe_entry);
    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to load trek ELF\n");
    }
    
    ioclose(trekio);
    
    kprintf("Trek program loaded, starting execution\n");
    
    // exe_entry(termio);
    
    kprintf("\n===== TEST 2: Running Trek in separate thread =====\n");
    
    tid = thread_spawn("trek", (void(*)(void))run_trek, termio);
    if (tid < 0) {
        kprintf("Error: %d\n", tid);
        panic("Failed to spawn trek thread\n");
    }
    
    kprintf("Trek thread created with ID: %d\n", tid);
    
    kprintf("Main thread waiting for trek to finish...\n");
    result = thread_join(tid);
    
    kprintf("Trek thread exited with result: %d\n", result);
    
    kprintf("\n===== ALL TESTS COMPLETED =====\n");
    
    while(1) {
        thread_yield();
    }
}