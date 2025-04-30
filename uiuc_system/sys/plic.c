// plic.c - RISC-V PLIC
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef PLIC_TRACE
#define TRACE
#endif

#ifdef PLIC_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "plic.h"
#include "assert.h"

#include <stdint.h>

// INTERNAL MACRO DEFINITIONS
//

#define pending_off 0x1000
#define enable_off 0x2000
#define threshold_off 0x200000
#define claim_off 0x200004
#define context_off  0x80
// CTX(i,0) is hartid /i/ M-mode context
// CTX(i,1) is hartid /i/ S-mode context

#define CTX(i,s) (2*(i)+(s))

// INTERNAL TYPE DEFINITIONS
// 

struct plic_regs {
	union {
		uint32_t priority[PLIC_SRC_CNT];
		char _reserved_priority[0x1000];
	};

	union {
		uint32_t pending[PLIC_SRC_CNT/32];
		char _reserved_pending[0x1000];
	};

	union {
		uint32_t enable[PLIC_CTX_CNT][32];
		char _reserved_enable[0x200000-0x2000];
	};

	struct {
		union {
			struct {
				uint32_t threshold;
				uint32_t claim;
			};
			
			char _reserved_ctxctl[0x1000];
		};
	} ctx[PLIC_CTX_CNT];
};

#define PLIC (*(volatile struct plic_regs*)PLIC_MMIO_BASE)

// INTERNAL FUNCTION DECLARATIONS
//

static void plic_set_source_priority (
	uint_fast32_t srcno, uint_fast32_t level);
static int plic_source_pending(uint_fast32_t srcno);
static void plic_enable_source_for_context (
	uint_fast32_t ctxno, uint_fast32_t srcno);
static void plic_disable_source_for_context (
	uint_fast32_t ctxno, uint_fast32_t srcno);
static void plic_set_context_threshold (
	uint_fast32_t ctxno, uint_fast32_t level);
static uint_fast32_t plic_claim_context_interrupt (
	uint_fast32_t ctxno);
static void plic_complete_context_interrupt (
	uint_fast32_t ctxno, uint_fast32_t srcno);

static void plic_enable_all_sources_for_context(uint_fast32_t ctxno);
static void plic_disable_all_sources_for_context(uint_fast32_t ctxno);

// We currently only support single-hart operation, sending interrupts to S mode
// on hart 0 (context 0). The low-level PLIC functions already understand
// contexts, so we only need to modify the high-level functions (plit_init,
// plic_claim_request, plic_finish_request)to add support for multiple harts.

// EXPORTED FUNCTION DEFINITIONS
// 

void plic_init(void) {
	int i;

	// Disable all sources by setting priority to 0

	for (i = 0; i < PLIC_SRC_CNT; i++)
		plic_set_source_priority(i, 0);
	
	// Route all sources to S mode on hart 0 only

	for (int i = 0; i < PLIC_CTX_CNT; i++)
		plic_disable_all_sources_for_context(i);
	
	plic_enable_all_sources_for_context(CTX(0,1));
}
extern void plic_enable_source(int srcno, int prio) {
	trace("%s(srcno=%d,prio=%d)", __func__, srcno, prio);
	assert (0 < srcno && srcno <= PLIC_SRC_CNT);
	assert (prio > 0);
	plic_set_source_priority(srcno, prio);
}

extern void plic_disable_source(int irqno) {
	if (0 < irqno)
		plic_set_source_priority(irqno, 0);
	else
		debug("plic_disable_irq called with irqno = %d", irqno);
}

extern int plic_claim_interrupt(void) {
	// FIXME: Hardwired S-mode hart 0
	trace("%s()", __func__);
	return plic_claim_context_interrupt(CTX(0,1));

}

extern void plic_finish_interrupt(int irqno) {
	trace("%s(irqno=%d)", __func__, irqno);
	plic_complete_context_interrupt(CTX(0,1), irqno);
	// FIXME: Hardwired S-mode hart 0
}

// INTERNAL FUNCTION DEFINITIONS
//

// static inline void plic_set_source_priority(uint_fast32_t srcno, uint_fast32_t level)
// Inputs: 
//   uint_fast32_t srcno - The source number of the interrupt
//   uint_fast32_t level - The priority level to set for the interrupt source
// Outputs: 
//   None
// Description: 
//   Sets the priority level for the specified interrupt source.
// Side Effects: 
//   Modifies the priority register for the specified interrupt source.
static inline void plic_set_source_priority(uint_fast32_t srcno, uint_fast32_t level) {
	// FIXME your code goes here
	volatile uint32_t *priority_reg = (volatile uint32_t *)((uintptr_t)(PLIC_MMIO_BASE) + srcno * 4);//calculate the address of the priority register
	*priority_reg = level;//set the priority level
}


// static inline int plic_source_pending(uint_fast32_t srcno)
// Inputs: 
//   uint_fast32_t srcno - The source number of the interrupt
// Outputs: 
//   int - Returns 1 if the interrupt source is pending, 0 otherwise
// Description: 
//   Checks if the specified interrupt source is pending.
// Side Effects: 
//   None
static inline int plic_source_pending(uint_fast32_t srcno) {
	volatile uint32_t *pending_reg = (volatile uint32_t *)((uintptr_t)(PLIC_MMIO_BASE) + pending_off );    //calculate the address of the pending register  
    uint32_t reg_index = srcno / 32;    //calculate the index of the register
    uint32_t bit_index = srcno % 32;    //calculate the index of the bit
    return (pending_reg[reg_index]&1<<bit_index)!=0;   //return the bool value
	// FIXME your code goes here
}

// static inline void plic_enable_source_for_context(uint_fast32_t ctxno, uint_fast32_t srcno)
// Inputs: 
//   uint_fast32_t ctxno - The context number
//   uint_fast32_t srcno - The source number of the interrupt
// Outputs: 
//   None
// Description: 
//   Enables the specified interrupt source for the given context.
// Side Effects: 
//   Modifies the enable register for the specified context and interrupt source.
static inline void plic_enable_source_for_context(uint_fast32_t ctxno, uint_fast32_t srcno) {
	// FIXME your code goes here
	volatile uint32_t *enable_reg = (volatile uint32_t *)((uintptr_t)(PLIC_MMIO_BASE) + enable_off + ctxno * context_off);    //calculate the address of the enable register
    uint32_t reg_index = srcno / 32;    //calculate the index of the register
    uint32_t bit_index = srcno %32;    //calculate the index of the bit
    enable_reg[reg_index] |= (1<<bit_index);    //enable the source for the context
}

// static inline void plic_disable_source_for_context(uint_fast32_t ctxno, uint_fast32_t srcid)
// Inputs: 
//   uint_fast32_t ctxno - The context number
//   uint_fast32_t srcid - The source number of the interrupt
// Outputs: 
//   None
// Description: 
//   Disables the specified interrupt source for the given context.
// Side Effects: 
//   Modifies the enable register for the specified context and interrupt source.
static inline void plic_disable_source_for_context(uint_fast32_t ctxno, uint_fast32_t srcid) {
	// FIXME your code goes here
	volatile uint32_t *enable_reg = (volatile uint32_t *)((uintptr_t)(PLIC_MMIO_BASE) + enable_off + ctxno *context_off);    //calculate the address of the enable register
    uint32_t reg_index = srcid / 32;    //calculate the index of the register
    uint32_t bit_index = srcid %32;    //calculate the index of the bit
    enable_reg[reg_index] &= ~(1<<bit_index);    //disable the source for the context
}

// static inline void plic_set_context_threshold(uint_fast32_t ctxno, uint_fast32_t level)
// Inputs: 
//   uint_fast32_t ctxno - The context number
//   uint_fast32_t level - The threshold level to set for the context
// Outputs: 
//   None
// Description: 
//   Sets the threshold level for the specified context.
// Side Effects: 
//   Modifies the threshold register for the specified context.
static inline void plic_set_context_threshold(uint_fast32_t ctxno, uint_fast32_t level) {
	// FIXME your code goes here
	volatile uint32_t *threshold_reg = (volatile uint32_t *)((uintptr_t)(PLIC_MMIO_BASE) + threshold_off + ctxno * pending_off);    //calculate the address of the threshold register
    *threshold_reg = level; //set the threshold level
}

// static inline uint_fast32_t plic_claim_context_interrupt(uint_fast32_t ctxno)
// Inputs:
//   uint_fast32_t ctxno - The context number
// Outputs:
//   uint_fast32_t - The source number of the interrupt
// Description:
//   Claims the next pending interrupt for the specified context.
// Side Effects:
//	Reads the claim register for the specified context.
static inline uint_fast32_t plic_claim_context_interrupt(uint_fast32_t ctxno) {
		volatile uint32_t *claim_reg = (volatile uint32_t *)((uintptr_t)(PLIC_MMIO_BASE) + claim_off + ctxno * pending_off);    //calculate the address of the claim register
		return *claim_reg;    //return the value of the claim register
	
}
// static inline void plic_complete_context_interrupt(uint_fast32_t ctxno, uint_fast32_t srcno)
// Inputs:
//   uint_fast32_t ctxno - The context number
//   uint_fast32_t srcno - The source number of the interrupt
// Outputs:
//   None
// Description:
//   Completes the interrupt for the specified context and source.
// Side Effects:
//	Writes to the claim register for the specified context.
static inline void plic_complete_context_interrupt(uint_fast32_t ctxno, uint_fast32_t srcno) {
	// FIXME your code goes here
	volatile uint32_t *claim_reg = (volatile uint32_t *)((uintptr_t)(PLIC_MMIO_BASE) + claim_off + ctxno *pending_off);//calculate the address of the claim register
    *claim_reg = srcno;//complete the interrupt
}

//static void plic_enable_all_sources_for_context(uint_fast32_t ctxno)
// Inputs:
//   uint_fast32_t ctxno - The context number
// Outputs:
//   None
// Description:
//   Enables all interrupt sources for the specified context.
// Side Effects:
//   Modifies the enable register for the specified context.

static void plic_enable_all_sources_for_context(uint_fast32_t ctxno) {
	// FIXME your code goes here
    if (ctxno >= PLIC_CTX_CNT) {//check if the context number is valid
        return;
    }
    volatile uint32_t *enable_all = (volatile uint32_t *)((uintptr_t)(PLIC_MMIO_BASE) + enable_off + ctxno * context_off);//calculate the address of the enable register
    for (unsigned int i = 0; i < (PLIC_SRC_CNT + 31) / 32; i++) {//enable all sources
        enable_all[i] = ~0;
    }
}
//static void plic_disable_all_sources_for_context(uint_fast32_t ctxno)
// Inputs:
//   uint_fast32_t ctxno - The context number
// Outputs:
//   None
// Description:
//   Disables all interrupt sources for the specified context.
// Side Effects:
//   Modifies the enable register for the specified context.
static void plic_disable_all_sources_for_context(uint_fast32_t ctxno) {
	// FIXME your code goes here
    if (ctxno >= PLIC_CTX_CNT) {	//check if the context number is valid
        return;
    }
    volatile uint32_t *enable_all = (volatile uint32_t *)((uintptr_t)(PLIC_MMIO_BASE) + enable_off + ctxno * context_off);//calculate the address of the enable register
    for (unsigned int i = 0; i < (PLIC_SRC_CNT + 31) / 32; i++) {
        enable_all[i] = 0;//disable all sources
    }
}