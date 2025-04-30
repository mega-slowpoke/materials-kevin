// uart.c - NS8250-compatible uart port
// 
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

// #define UART_DEBUG
// #ifdef UART_TRACE
// #define TRACE
// #endif

#ifdef UART_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "assert.h"
#include "uart.h"
#include "device.h"
#include "intr.h"
#include "heap.h"
#include "thread.h"

#include "ioimpl.h"
#include "console.h"

#include "error.h"

#include <stdint.h>

// COMPILE-TIME CONSTANT DEFINITIONS
//

#ifndef UART_RBUFSZ
#define UART_RBUFSZ 64
#endif

#ifndef UART_INTR_PRIO
#define UART_INTR_PRIO 1
#endif

#ifndef UART_NAME
#define UART_NAME "uart"
#endif

// INTERNAL TYPE DEFINITIONS
// 

struct uart_regs {
    union {
        char rbr; // DLAB=0 read
        char thr; // DLAB=0 write
        uint8_t dll; // DLAB=1
    };
    
    union {
        uint8_t ier; // DLAB=0
        uint8_t dlm; // DLAB=1
    };
    
    union {
        uint8_t iir; // read
        uint8_t fcr; // write
    };

    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr;
    uint8_t msr;
    uint8_t scr;
};

#define LCR_DLAB (1 << 7)
#define LSR_OE (1 << 1)
#define LSR_DR (1 << 0)
#define LSR_THRE (1 << 5)
#define IER_DRIE (1 << 0)
#define IER_THREIE (1 << 1)

struct ringbuf {
    unsigned int hpos; // head of queue (from where elements are removed)
    unsigned int tpos; // tail of queue (where elements are inserted)
    char data[UART_RBUFSZ];
};

struct uart_device {
    volatile struct uart_regs * regs;
    int irqno;
    int instno;

    struct io io;

    unsigned long rxovrcnt; // number of times OE was set

    struct ringbuf rxbuf;
    struct ringbuf txbuf;

    struct condition rx_cond;
    struct condition tx_cond;
};

// INTERNAL FUNCTION DEFINITIONS
//

static int uart_open(struct io ** ioptr, void * aux);
static void uart_close(struct io * io);
static long uart_read(struct io * io, void * buf, long bufsz);
static long uart_write(struct io * io, const void * buf, long len);

static void uart_isr(int srcno, void * driver_private);

static void rbuf_init(struct ringbuf * rbuf);
static int rbuf_empty(const struct ringbuf * rbuf);
static int rbuf_full(const struct ringbuf * rbuf);
static void rbuf_putc(struct ringbuf * rbuf, char c);
static char rbuf_getc(struct ringbuf * rbuf);

// EXPORTED FUNCTION DEFINITIONS
// 

void uart_attach(void * mmio_base, int irqno) {
    static const struct iointf uart_iointf = {
        .close = &uart_close,
        .read = &uart_read,
        .write = &uart_write
    };

    struct uart_device * uart;

    uart = kcalloc(1, sizeof(struct uart_device));

    uart->regs = mmio_base;
    uart->irqno = irqno;

    ioinit0(&uart->io, &uart_iointf);

    // Check if we're trying to attach UART0, which is used for the console. It
    // had already been initialized and should not be accessed as a normal
    // device.

    if (mmio_base != (void*)UART0_MMIO_BASE) {

        uart->regs->ier = 0;
        uart->regs->lcr = LCR_DLAB;
        // fence o,o ?
        uart->regs->dll = 0x01;
        uart->regs->dlm = 0x00;
        // fence o,o ?
        uart->regs->lcr = 0; // DLAB=0

        uart->instno = register_device(UART_NAME, uart_open, uart);

    } else
        uart->instno = register_device(UART_NAME, NULL, NULL);
}
//int uart_open(struct io ** ioptr, void * aux)
// Inputs:
//   struct io ** ioptr - Pointer to the I/O interface
//   void * aux - Pointer to the UART device structure
// Outputs:
//   int - Returns 0 if the UART device is successfully opened, -EBUSY if the device is already open
// Description:
//   Opens the UART device.
// Side Effects:
//   Initializes the receive and transmit buffers, enables the UART device, and enables the UART interrupt.
int uart_open(struct io ** ioptr, void * aux) {
    struct uart_device * const uart = aux;

    trace("%s()", __func__);

    if (iorefcnt(&uart->io) != 0)
        return -EBUSY;
    
    // Reset receive and transmit buffers
    
    rbuf_init(&uart->rxbuf);
    rbuf_init(&uart->txbuf);

    // Read receive buffer register to flush any stale data in hardware buffer
    condition_init(&uart->rx_cond, "uart_rx");
    condition_init(&uart->tx_cond, "uart_tx");
    uart->regs->rbr; // forces a read because uart->regs is volatile
    uart->regs->ier |= IER_DRIE;
    enable_intr_source(uart->irqno, UART_INTR_PRIO, uart_isr, uart);
    *ioptr = &uart->io;
    uart->io.refcnt++;
    // FIXME your code goes here

    return 0;
}

// void uart_close(struct io * io)
// Inputs:
//   struct io * io - The I/O interface for the UART device
// Outputs:
//   None
// Description:
//   Closes the UART device.
// Side Effects:
//   Disables the UART device and decrements the reference count of the I/O interface.
void uart_close(struct io * io) {
    struct uart_device * const uart =
        (void*)io - offsetof(struct uart_device, io); // pointer to the uart_device structure

    trace("%s()", __func__); // print the function name
    assert (iorefcnt(io) == 0); // check if the reference count is 0
    uart->regs->ier &= ~(IER_DRIE | IER_THREIE);// disable the interrupts
    disable_intr_source(uart->irqno);// disable the interrupt source
    condition_broadcast(&uart->rx_cond);// broadcast the condition
    condition_broadcast(&uart->tx_cond);// broadcast the condition
    uart->io.refcnt--;// decrement the reference count
    // FIXME your code goes here

}
// long uart_read(struct io * io, void * buf, long bufsz)
// Inputs:
//   struct io * io - The I/O interface for the UART device
//   void * buf - The buffer to read data into
//   long bufsz - The number of bytes to read
// Outputs:
//   long - Returns the number of bytes read
// Description:
//   Reads data from the UART device.
// Side Effects:
//   Modifies the receive buffer and updates the UART registers.
long uart_read(struct io * io, void * buf, long bufsz) {
    // FIXME your code goes here
    struct uart_device * const uart = (void*)io - offsetof(struct uart_device, io);
    char *cbuf = buf;
    long bytes_read = 0;

    // enable receive interrupt
    uart->regs->ier |= IER_DRIE;

    // read from the receive buffer
    while (bytes_read < bufsz) {
    // 
        while (rbuf_empty(&uart->rxbuf)) {
            uart->regs->ier |= IER_DRIE;
            if (!thrmgr_initialized){
                while(rbuf_empty(&uart->rxbuf)&&((uart->regs->lsr & LSR_DR)==0)){
                }
                continue;
            }
            condition_wait(&uart->rx_cond);
}
    // 
    cbuf[bytes_read++] = rbuf_getc(&uart->rxbuf);
}

    return bytes_read;
}
// long uart_write(struct io * io, const void * buf, long len)
// Inputs:
//   struct io * io - The I/O interface for the UART device
//   const void * buf - The buffer to write data from
//   long len - The number of bytes to write
// Outputs:
//   long - Returns the number of bytes written
// Description:
//   Writes data to the UART device.
// Side Effects:
//   Modifies the transmit buffer and updates the UART registers.
long uart_write(struct io * io, const void * buf, long len) {
    // FIXME your code goes here
    struct uart_device * const uart = (void*)io - offsetof(struct uart_device, io);
    const char *cbuf = buf;
    long bytes_written = 0;

    while (bytes_written < len) {
        while (rbuf_full(&uart->txbuf)) {
            uart->regs->ier |= IER_THREIE;
            if (!thrmgr_initialized){
                while(rbuf_full(&uart->txbuf)&& !((uart->regs->lsr & LSR_THRE) == 0)){
                }
                continue;
            }
            condition_wait(&uart->tx_cond);
    }

    rbuf_putc(&uart->txbuf, cbuf[bytes_written++]);
    uart->regs->ier |= IER_THREIE;
}

    return bytes_written;
}

// void uart_isr(int srcno, void * aux)
// Inputs: 
//   int srcno - The source number of the interrupt
//   void * aux - Auxiliary data, typically a pointer to the uart_device structure
// Outputs: 
//   None
// Description: 
//   Interrupt service routine for the UART device. Handles receive and transmit interrupts.
// Side Effects: 
//   Modifies the receive and transmit buffers, and updates the UART registers.
void uart_isr(int srcno, void * aux) {
    debug("UART ISR called: srcno=%d", srcno);
    // FIXME your code goes here
    struct uart_device * const uart = aux; // pointer to the uart_device structure
    if(!uart || !uart->regs) return;
    uint8_t lsr = uart->regs->lsr; // read the line status register
    // if (lsr & LSR_OE) {
    //     uart->rxovrcnt++;
    // }
// Handle receive data ready
    if (lsr & LSR_DR) { // check if data is ready
        char data = uart->regs->rbr; // read the data
        if (!rbuf_full(&uart->rxbuf)) { // check if the buffer is full
            rbuf_putc(&uart->rxbuf, data);// put the data in the buffer
            if (thrmgr_initialized){
            condition_broadcast(&uart->rx_cond);
            }// broadcast the condition
    } 
    else {
// Buffer full, disable receive interrupt
        uart->regs->ier &= ~IER_DRIE;
    }
    }
// Check for overrun error
// Handle transmit holding register empty
    if (lsr & LSR_THRE) {
        if (!rbuf_empty(&uart->txbuf)) {
            uart->regs->thr = rbuf_getc(&uart->txbuf);
            if (thrmgr_initialized){
                condition_broadcast(&uart->tx_cond);
                }
    } 
    else {
// No more data to send, disable transmit interrupt
    uart->regs->ier &= ~IER_THREIE;
        }
        ////////////////////////////////////////////////
        if (lsr & LSR_OE) {
            uart->rxovrcnt++;
        }
    }
}


void rbuf_init(struct ringbuf * rbuf) {
    rbuf->hpos = 0;
    rbuf->tpos = 0;
}


int rbuf_empty(const struct ringbuf * rbuf) {
    return (rbuf->hpos == rbuf->tpos);
}
int rbuf_full(const struct ringbuf * rbuf) {
    return ((uint16_t)(rbuf->tpos - rbuf->hpos) == UART_RBUFSZ);
}

void rbuf_putc(struct ringbuf * rbuf, char c) {
    uint_fast16_t tpos;

    tpos = rbuf->tpos;
    rbuf->data[tpos % UART_RBUFSZ] = c;
    asm volatile ("" ::: "memory");
    rbuf->tpos = tpos + 1;
}

char rbuf_getc(struct ringbuf * rbuf) {
    uint_fast16_t hpos;
    char c;

    hpos = rbuf->hpos;
    c = rbuf->data[hpos % UART_RBUFSZ];
    asm volatile ("" ::: "memory");
    rbuf->hpos = hpos + 1;
    return c;
}

// The functions below provide polled uart input and output for the console.

#define UART0 (*(volatile struct uart_regs*)UART0_MMIO_BASE)

void console_device_init(void) {
    UART0.ier = 0x00;

    // Configure UART0. We set the baud rate divisor to 1, the lowest value,
    // for the fastest baud rate. In a physical system, the actual baud rate
    // depends on the attached oscillator frequency. In a virtualized system,
    // it doesn't matter.
    
    UART0.lcr = LCR_DLAB;
    UART0.dll = 0x01;
    UART0.dlm = 0x00;

    // The com0_putc and com0_getc functions assume DLAB=0.

    UART0.lcr = 0;
}

void console_device_putc(char c) {
    // Spin until THR is empty
    while (!(UART0.lsr & LSR_THRE))
        continue;

    UART0.thr = c;
}

char console_device_getc(void) {
    // Spin until RBR contains a byte
    while (!(UART0.lsr & LSR_DR))
        continue;
    
    return UART0.rbr;
}