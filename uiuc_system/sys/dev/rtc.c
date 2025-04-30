// rtc.c - Goldfish RTC driver
// 
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef RTC_TRACE
#define TRACE
#endif

#ifdef RTC_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "assert.h"
#include "rtc.h"
#include "device.h"
#include "ioimpl.h"
#include "console.h"
#include "string.h"
#include "heap.h"

#include "error.h"

#include <stdint.h>

#ifndef EINVAL
#define EINVAL 1
#endif
#ifndef ENOTSUP
#define ENOTSUP 3
#endif
#ifndef EBUSY
#define EBUSY 2
#endif
#ifndef ENOMEM
#define ENOMEM 14
#endif

// INTERNAL TYPE DEFINITIONS
#define RTC_NAME "rtc"
// 

struct rtc_regs {
    // TODO
    volatile uint32_t time_low;
    volatile uint32_t time_high;
    //these two values can be changed at any time.
};

struct rtc_device {
    // TODO
    volatile struct rtc_regs* regs;
    struct io io;
    int instno;
};

// INTERNAL FUNCTION DEFINITIONS
//

static int rtc_open(struct io ** ioptr, void * aux);
static void rtc_close(struct io * io);
static int rtc_cntl(struct io * io, int cmd, void * arg);
static long rtc_read(struct io * io, void * buf, long bufsz);

static uint64_t read_real_time(struct rtc_regs * regs);

// EXPORTED FUNCTION DEFINITIONS
// 

// input: memory pointer to the base address of the memory-mapped RTC registers
// this function attaches the device with the system. It registers the device with the system, set up the IO interface and its memory-mapped registers
void rtc_attach(void * mmio_base) {
    // TODO
    //allocate memory for the rtc_device dynamically
    struct rtc_device* rtc = kcalloc(1, sizeof(struct rtc_device));
    if(rtc == NULL){
        return;
    }
    //link the mmio_base to the memory mapped RTC registers
    rtc -> regs = (volatile struct rtc_regs*)mmio_base;
    //initialize the IO subsystem
    static const struct iointf rtc_iointf = {
        .close = &rtc_close,
        .cntl = &rtc_cntl,
        .read = &rtc_read
    };
    //now register the device with the system and assign it an instance number
    ioinit0(&rtc -> io, &rtc_iointf);
    rtc -> instno = register_device(RTC_NAME, rtc_open, rtc);

}

// input: ioptr contains a pointer to store the reference to the RTC IO structure for an RTC instance. 
//        aux contains a pointer to the RTC device structure
// This function associates an IO reference with the RTC device, allowing it to be used by other system components. 
// and ensures the device is properly referenced before returning control
int rtc_open(struct io ** ioptr, void * aux) {
    // TODO
    //set the rtc pointer ready to operate
    struct rtc_device* const rtc = (struct rtc_device*)aux;
    //check if the device is already open
    if(iorefcnt(&rtc -> io) != 0){
        return -EBUSY;
    }
    //check if the pointer is a NULL pointer
    if(rtc == NULL){
        return -EINVAL;
    }
    //associate the IO reference with the RTC device
    *ioptr = &rtc -> io;
    //increment the reference count
    rtc -> io.refcnt += 1;

    return 0;
}

// input: io contains a pointer to the IO structure associated with the RTC device.
// special notice: this function should make sure the refcnt is 0
void rtc_close(struct io * io) {
    // TODO
    // struct rtc_device * const rtc = (void*)io - offsetof(struct rtc_device, io);
    if(io== NULL){
        return;
    }
    //at this point, file is successfully closed
    io -> refcnt -= 1;
    if(io -> refcnt != 0){
        return;
    }
    // kfree(rtc);
}

// input: io is a pointer to the RTC IO structure
//        cmd is the number or aliase of the operation
//        arg is a pointer for the argument of the command
// This function only needs to support querying the block size of the RTC data. It checks if cmd
// matches the desired command for block size and returns the block size value. Otherwise, the function returns
// the error code -ENOTSUP, indicating that the operation is not supported
int rtc_cntl(struct io * io, int cmd, void * arg) {
    // TODO
    //struct rtc_device * const rtc = (void*)io - offsetof(struct rtc_device, io);
    // if(io -> intf -> cntl != NULL){
    //     return io -> intf -> cntl(rtc, cmd, arg);
    // }
    if(io == NULL){
        return -EINVAL;
    }
    else if(cmd == IOCTL_GETBLKSZ){
        return 8; //since time_low and time_high are both 4 bytes and together it is 8 bytes
    }
    else{
        return -ENOTSUP;
    }
}

// input: io is a pointer to the RTC IO structure. 
//        buf is a pointer to the buffer where the timestamp will be stored. 
//        bufsz is the size of the buffer in bytes
// This function calculates the base address of the rtc device structure by using offsetof on
// the provided pointer. It should then ensure that the buffer is large enough to store the 64-bit timestamp before
// performing the read operation using the helper function read real time. This function returns the size of data
// written (8 bytes).
long rtc_read(struct io * io, void * buf, long bufsz) {
    // TODO
    //calculate the base address considering the io offset
    struct rtc_device * const rtc = (void*)io - offsetof(struct rtc_device, io);
    //if buffer is not large enough(not enough for 64 bits)
    if(bufsz < 8){
        return -ENOMEM;
    }
    int timestamp = read_real_time((struct rtc_regs *)(rtc -> regs));
    //set a memory place for the newly read time
    memcpy(buf, &timestamp, 8);
    return 8;
}

// input: a pointer to the registers for RTC
// This function reads the time’s low 32 bits, then the high 32 bits. It combines these to form
// and return the full 64-bit timestamp.
uint64_t read_real_time(struct rtc_regs * regs) {
    // TODO
    uint32_t lower_bits = regs -> time_low;
    uint32_t higher_bits = regs -> time_high;
    // directly read from two registers
    uint64_t combined = ((uint64_t)higher_bits << 32 | lower_bits);
    return combined;
}