// timer.c
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

// #define TIMER_TRACE
// #define TIMER_DEBUG
// #ifdef TIMER_TRACE
// #define TRACE
// #endif

#ifdef TIMER_DEBUG
#define DEBUG
#endif

#include "timer.h"
#include "thread.h"
#include "riscv.h"
#include "assert.h"
#include "intr.h"
#include "conf.h"
#include "see.h" // for set_stcmp

// EXPORTED GLOBAL VARIABLE DEFINITIONS
// 

char timer_initialized = 0;

// INTERNVAL GLOBAL VARIABLE DEFINITIONS
//

static struct alarm * sleep_list;

// INTERNAL FUNCTION DECLARATIONS
//

// EXPORTED FUNCTION DEFINITIONS
//

void timer_init(void) {
    set_stcmp(UINT64_MAX);
    timer_initialized = 1;
}
//input: al - pointer to alarm structure
//input: name - name of the alarm
//output: none
//description: initializes the alarm structure
//side effects: initializes the alarm structure
void alarm_init(struct alarm * al, const char * name) {
    condition_init(&al->cond,name); // initialize the condition variable
    al->next = NULL;
    al->twake = rdtime();       // initialize the wake-up time
    // FIXME your code goes here
}
//input: al - pointer to alarm structure
//input: tcnt - time increment
//output: none
//description: sleeps for the specified time increment
//side effects: sleeps for the specified time increment
void alarm_sleep(struct alarm * al, unsigned long long tcnt) {
    unsigned long long now;
    struct alarm * prev;
    int pie;

    now = rdtime();

    // If the tcnt is so large it wraps around, set it to UINT64_MAX

    if (UINT64_MAX - al->twake < tcnt)
        al->twake = UINT64_MAX;
    else
        al->twake += tcnt;
    
    // If the wake-up time has already passed, return

    if (al->twake < now)
        return;
    pie = disable_interrupts();

        // Insert the alarm into the sleep list in order of increasing twake
    if (sleep_list == NULL || al->twake < sleep_list->twake) {
        // Insert at the head
        al->next = sleep_list;
        sleep_list = al;
        
        // Update the timer compare register to trigger at this alarm's time
        set_stcmp(al->twake);
        } 
    else {
        // Find the correct position in the list
        prev = sleep_list;
        while (prev->next != NULL && prev->next->twake <= al->twake)
        prev = prev->next;
        
        // Insert after prev
        al->next = prev->next;
        prev->next = al;
        }
        //////////
        debug("Alarm added to sleep_list: al=%p, twake=%llu, now=%llu, sleep_list=%p",
            al, al->twake, now, sleep_list);
        /////////
        csrs_sie(RISCV_SIE_STIE);
        restore_interrupts(pie);
        condition_wait(&al->cond);
    // FIXME your code goes here
}

// Resets the alarm so that the next sleep increment is relative to the time
// alarm_reset is called.

void alarm_reset(struct alarm * al) {
    al->twake = rdtime();
}

void alarm_sleep_sec(struct alarm * al, unsigned int sec) {
    alarm_sleep(al, sec * TIMER_FREQ);
}

void alarm_sleep_ms(struct alarm * al, unsigned long ms) {
    alarm_sleep(al, ms * (TIMER_FREQ / 1000));
}

void alarm_sleep_us(struct alarm * al, unsigned long us) {
    alarm_sleep(al, us * (TIMER_FREQ / 1000 / 1000));
}

void sleep_sec(unsigned int sec) {
    sleep_ms(1000UL * sec);
}

void sleep_ms(unsigned long ms) {
    sleep_us(1000UL * ms);
}

void sleep_us(unsigned long us) {
    struct alarm al;

    alarm_init(&al, "sleep");
    alarm_sleep_us(&al, us);
}

// handle_timer_interrupt() is dispatched from intr_handler in intr.c
//input: none
//output: none
//description: handles the timer interrupt
//side effects: handles the timer interrupt
void handle_timer_interrupt(void) {
    struct alarm * head = sleep_list;
    uint64_t now;

    now = rdtime();

    trace("[%lu] %s()", now, __func__);
    // debug("[%lu] mtcmp = %lu", now, rdtime());
    debug("[%lu] sleep_list = %p", now, sleep_list);
    // FIXME your code goes here
    if (head == NULL) {
        // No alarms, disable timer interrupts
        csrc_sie(RISCV_SIE_STIE);
        return;
        }
        
        // Check if any alarms have elapsed
    while (head != NULL && head->twake <= now) {
        // Remove the alarm from the list
        sleep_list = head->next;
        debug("[%lu] Broadcasting condition %p with name %s", now, &head->cond, head->cond.name);
        // Wake up the thread waiting on this alarm
        condition_broadcast(&head->cond);
        
        // Move to the next alarm
        head = sleep_list;
        }
    if (sleep_list != NULL) {
        // Set the timer compare register to the next alarm's time
        set_stcmp(sleep_list->twake);
        //newwwwwwwwwwwwwwwww
        csrs_sie(RISCV_SIE_STIE);
        } 
    else {
        // No alarms, disable timer interrupts
        csrc_sie(RISCV_SIE_STIE);
        }
        
}