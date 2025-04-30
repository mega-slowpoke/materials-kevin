
// thread.c - Threads
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//


// #ifdef THREAD_TRACE
// #define TRACE
// #endif

// #ifdef THREAD_DEBUG
// #define DEBUG
// #endif

#include "thread.h"

#include <stddef.h>
#include <stdint.h>

#include "assert.h"
#include "heap.h"
#include "string.h"
#include "riscv.h"
#include "intr.h"
#include "memory.h"
#include "error.h"

#include <stdarg.h>

// COMPILE-TIME PARAMETERS
//

// NTHR is the maximum number of threads

#ifndef NTHR
#define NTHR 16
#endif

#ifndef STACK_SIZE
#define STACK_SIZE 4000
#endif

// EXPORTED GLOBAL VARIABLES
//

char thrmgr_initialized = 0;

// INTERNAL TYPE DEFINITIONS
//

enum thread_state {
    THREAD_UNINITIALIZED = 0,
    THREAD_WAITING,
    THREAD_SELF,
    THREAD_READY,
    THREAD_EXITED
};


struct thread_context {
    uint64_t s[12];
    void * ra;
    void * sp;
};

struct thread_stack_anchor {
    struct thread * ktp;
    void * kgp;
};

struct thread {
    struct thread_context ctx;  // must be first member (thrasm.s)
    int id; // index into thrtab[]
    enum thread_state state;
    const char * name;
    struct thread_stack_anchor * stack_anchor;
    void * stack_lowest;
    struct thread * parent;
    struct thread * list_next;
    struct condition * wait_cond;
    struct condition child_exit;
    struct lock * lock_list;
};

// INTERNAL MACRO DEFINITIONS
// 

// Pointer to running thread, which is kept in the tp (x4) register.

#define TP ((struct thread*)__builtin_thread_pointer())

// Macro for changing thread state. If compiled for debugging (DEBUG is
// defined), prints function that changed thread state.

#define set_thread_state(t,s) do { \
    debug("Thread <%s:%d> state changed from %s to %s by <%s:%d> in %s", \
        (t)->name, (t)->id, \
        thread_state_name((t)->state), \
        thread_state_name(s), \
        TP->name, TP->id, \
        __func__); \
    (t)->state = (s); \
} while (0)

// INTERNAL FUNCTION DECLARATIONS
//

// Initializes the main and idle threads. called from threads_init().
void lock_release(struct lock *lock);
static void init_main_thread(void);
static void init_idle_thread(void);

// Sets the RISC-V thread pointer to point to a thread.

static void set_running_thread(struct thread * thr);

// Returns a string representing the state name. Used by debug and trace
// statements, so marked unused to avoid compiler warnings.

static const char * thread_state_name(enum thread_state state)
    __attribute__ ((unused));

// void thread_reclaim(int tid)
//
// Reclaims a thread's slot in thrtab and makes its parent the parent of its
// children. Frees the struct thread of the thread.

static void thread_reclaim(int tid);

// struct thread * create_thread(const char * name)
//
// Creates and initializes a new thread structure. The new thread is not added
// to any list and does not have a valid context (_thread_switch cannot be
// called to switch to the new thread).

static struct thread * create_thread(const char * name);

// void running_thread_suspend(void)
// Suspends the currently running thread and resumes the next thread on the
// ready-to-run list using _thread_swtch (in threasm.s). Must be called with
// interrupts enabled. Returns when the current thread is next scheduled for
// execution. If the current thread is TP, it is marked READY and placed
// on the ready-to-run list. Note that running_thread_suspend will only return if the
// current thread becomes READY.

static void running_thread_suspend(void);

// The following functions manipulate a thread list (struct thread_list). Note
// that threads form a linked list via the list_next member of each thread
// structure. Thread lists are used for the ready-to-run list (ready_list) and
// for the list of waiting threads of each condition variable. These functions
// are not interrupt-safe! The caller must disable interrupts before calling any
// thread list function that may modify a list that is used in an ISR.

static void tlclear(struct thread_list * list);
static int tlempty(const struct thread_list * list);
static void tlinsert(struct thread_list * list, struct thread * thr);
static struct thread * tlremove(struct thread_list * list);
static void tlappend(struct thread_list * l0, struct thread_list * l1);

static void idle_thread_func(void);

// IMPORTED FUNCTION DECLARATIONS
// defined in thrasm.s
//

extern struct thread * _thread_swtch(struct thread * thr);

extern void _thread_startup(void);

// INTERNAL GLOBAL VARIABLES
//

#define MAIN_TID 0
#define IDLE_TID (NTHR-1)

static struct thread main_thread;
static struct thread idle_thread;

extern char _main_stack_lowest[]; // from start.s
extern char _main_stack_anchor[]; // from start.s

static struct thread main_thread = {
    .id = MAIN_TID,
    .name = "main",
    .state = THREAD_SELF,
    .stack_anchor = (void*)_main_stack_anchor,
    .stack_lowest = _main_stack_lowest,
    .child_exit.name = "main.child_exit",
    .lock_list = NULL
};

extern char _idle_stack_lowest[]; // from thrasm.s
extern char _idle_stack_anchor[]; // from thrasm.s

static struct thread idle_thread = {
    .id = IDLE_TID,
    .name = "idle",
    .state = THREAD_READY,
    .parent = &main_thread,
    .stack_anchor = (void*)_idle_stack_anchor,
    .stack_lowest = _idle_stack_lowest,
    .ctx.sp = _idle_stack_anchor,
    .ctx.ra = &_thread_startup,
    .ctx.s[8] = (uint64_t)idle_thread_func,
    .lock_list = NULL
    // FIXME your code goes here
};

static struct thread * thrtab[NTHR] = {
    [MAIN_TID] = &main_thread,
    [IDLE_TID] = &idle_thread
};

static struct thread_list ready_list = {
    .head = &idle_thread,
    .tail = &idle_thread
};

// EXPORTED FUNCTION DEFINITIONS
//

int running_thread(void) {
    return TP->id;
}

void thrmgr_init(void) {
    trace("%s()", __func__);
    init_main_thread();
    init_idle_thread();
    set_running_thread(&main_thread);
    thrmgr_initialized = 1;
}

//input: name of the thread, entry function of the thread, arguments to the entry function
//output: thread id if successful, negative error code otherwise
//description: creates a new thread with the given name and entry function, and arguments to the entry function. 
//The new thread is added to the ready-to-run list and its context is initialized with the entry function and arguments. 
//The new thread is in the READY state. The function returns the thread id of the new thread if successful, or a negative error code otherwise. 
//side effects: allocates a new thread structure and stack, adds the new thread to the ready-to-run list
// and initializes its context, changes the state of the new thread to READY
// and adds it to the ready-to-run list
int thread_spawn (
    const char * name,
    void (*entry)(void),
    ...)
{
    struct thread * child;
    va_list ap;
    int pie;
    int i;

    child = create_thread(name);

    if (child == NULL)
        return -EMTHR;

    set_thread_state(child, THREAD_READY);

    pie = disable_interrupts();
    tlinsert(&ready_list, child);
    restore_interrupts(pie);

    // FIXME your code goes here
    child->ctx.ra = &_thread_startup;   // entry function address
    child->ctx.sp = child->stack_anchor;    // stack pointer
    child->ctx.s[8] = (uint64_t)entry;  // entry function address
    // filling in entry function arguments is given below, the rest is up to you

    va_start(ap, entry);
    for (i = 0; i < 8; i++)
        child->ctx.s[i] = va_arg(ap, uint64_t);
    va_end(ap);
    
    return child->id;
}
//input: none
//output: none
//description: exits the currently running thread. If the main thread exits, the system halts. If a thread exits, it is marked as exited and its parent is notified. The exited thread is suspended and the next thread on the ready-to-run list is resumed. If the exited thread is the last child of its parent, the parent is notified and suspended. If the exited thread is the last child of the main thread, the system halts. The function does not return.
//side effects: changes the state of the exited thread to EXITED, notifies the parent of the exited thread, suspends the exited thread, resumes the next thread on the ready-to-run list

void thread_exit(void) {
    struct lock * lock;
    if(TP->id == MAIN_TID){     // if the main thread exits, the system halts
        halt_success();
    }
    if (!TP || !TP->parent){        // if a thread exits, it is marked as exited and its parent is notified
        halt_failure();
    }
    while(TP->lock_list!=NULL){
        lock=TP->lock_list; // if the exited thread is the last child of its parent, the parent is notified 
        lock_release(lock);
    }
    set_thread_state(TP, THREAD_EXITED);    // changes the state of the exited thread to EXITED
    if(TP->parent){                 // notifies the parent of the exited thread
    condition_broadcast(&TP->parent->child_exit);
    }
    running_thread_suspend();       // suspends the exited thread, resumes the next thread on the ready-to-run list
    halt_failure();
    // FIXME your code goes here
}

void thread_yield(void) {
    trace("%s() in <%s:%d>", __func__, TP->name, TP->id);
    running_thread_suspend();
}
//input: thread id
//output: 0 if successful, negative error code otherwise
//description: waits for the specified thread to exit. If the specified thread has already exited, the function reclaims the thread slot and returns 0. If the specified thread has not exited, the function waits for the thread to exit. If the thread exits, the function reclaims the thread slot and returns 0. If the thread does not exit, the function does not return. If tid is 0, the function waits for any child thread of the current thread to exit. If no child thread of the current thread exists, the function returns -EINVAL.
//side effects: waits for the specified thread to exit, reclaims the thread slot if the thread exits
// and returns 0, does not return if the thread does not exit
int thread_join(int tid) {
    struct thread * child;
    int i, child_found = 0;     
    if (tid == 0) {         // if tid is 0, the function waits for any child thread of the current thread to exit
        for (i = 1; i < NTHR; i++) {
        if (thrtab[i] != NULL && thrtab[i]->parent == TP) {     // If no child thread of the current thread exists, the function returns -EINVAL
            child_found = 1;
            break;
            }
        }
        if (child_found == 0) {     
            return -EINVAL;
        }
        for (i = 1; i < NTHR; i++) {        // If the specified thread has already exited, the function reclaims the thread slot and returns 0
            if (thrtab[i] != NULL && thrtab[i]->parent == TP && thrtab[i]->state == THREAD_EXITED) {
                int tid_to_return = i;
                thread_reclaim(i);
                return tid_to_return;
                }
            }
            // If the specified thread has not exited, the function waits for the thread to exit
        if (TP->child_exit.wait_list.head == NULL && TP->child_exit.wait_list.tail == NULL) {
            condition_init(&TP->child_exit, "child_exit");
        }
            // If the thread exits, the function reclaims the thread slot and returns 0
        condition_wait(&TP->child_exit);
        // If the thread does not exit, the function does not return
        for (i = 1; i < NTHR; i++) {
            if (thrtab[i] != NULL && thrtab[i]->parent == TP && thrtab[i]->state == THREAD_EXITED) {
                int tid_to_return = i;
                thread_reclaim(i);
                return tid_to_return;
                }
            }
        } else {
                if (tid < 0 || tid >= NTHR || thrtab[tid] == NULL || thrtab[tid]->parent != TP) {
                return -EINVAL;
                }
                    // If the specified thread has already exited, the function reclaims the thread slot and returns 0
            child = thrtab[tid];
    
        if (child->state == THREAD_EXITED) {// If the specified thread has already exited, the function reclaims the thread slot and returns 0
            thread_reclaim(tid);
            return tid;
        }
        // If the specified thread has not exited, the function waits for the thread to exit
        if (TP->child_exit.wait_list.head == NULL && TP->child_exit.wait_list.tail == NULL) {
            condition_init(&TP->child_exit, "child_exit");
        }
    
    condition_wait(&TP->child_exit);
    // If the thread exits, the function reclaims the thread slot and returns 0
    if (thrtab[tid] != NULL && thrtab[tid]->state == THREAD_EXITED) {
    thread_reclaim(tid);
    return tid;
    }
    }
    
    return -EINVAL;
    
    // FIXME your code goes here
}

const char * thread_name(int tid) {
    assert (0 <= tid && tid < NTHR);
    assert (thrtab[tid] != NULL);
    return thrtab[tid]->name;
}

const char * running_thread_name(void) {
    return TP->name;
}

void condition_init(struct condition * cond, const char * name) {
    tlclear(&cond->wait_list);
    cond->name = name;
}
//input: condition variable
//output: none
//description: waits on the specified condition variable. The function adds the current thread to the wait list of the condition variable and suspends the current thread. The function does not return until the current thread is woken up by a call to condition_broadcast. The current thread is in the WAITING state while waiting on the condition variable.
//side effects: adds the current thread to the wait list of the condition variable, suspends the current thread
void condition_wait(struct condition * cond) {
    int pie;

    trace("%s(cond=<%s>) in <%s:%d>", __func__,
        cond->name, TP->name, TP->id);
       
    assert(TP->state == THREAD_SELF);

    // Insert current thread into condition wait list
    pie = disable_interrupts();
    set_thread_state(TP, THREAD_WAITING);// The current thread is in the WAITING state while waiting on the condition variable
    TP->wait_cond = cond;       // The function adds the current thread to the wait list of the condition variable
    TP->list_next = NULL;       // The function does not return until the current thread is woken up by a call to condition_broadcast
    tlinsert(&cond->wait_list, TP);// adds the current thread to the wait list of the condition variable
    running_thread_suspend();
    restore_interrupts(pie);// suspends the current thread
}
//input: condition variable
//output: none
//description: wakes up all threads waiting on the specified condition variable. The function moves all threads from the wait list of the condition variable to the ready-to-run list. The threads are in the READY state after waking up. The function does not return.
//side effects: moves all threads from the wait list of the condition variable to the ready-to-run list, the threads are in the READY state after waking up
void condition_broadcast(struct condition * cond) {
    struct thread_list waiting_threads; // List of threads waiting on the condition variable
    int pie;
    if (!cond) return;
    trace("%s(cond=<%s>)", __func__, cond->name);
    pie = disable_interrupts();
    tlclear(&waiting_threads);                  // List of threads waiting on the condition variable
    tlappend(&waiting_threads, &cond->wait_list);// List of threads waiting on the condition variable
    while (!tlempty(&waiting_threads)) {// moves all threads from the wait list of the condition variable to the ready-to-run list
        struct thread * thread = tlremove(&waiting_threads);
        if (thread->wait_cond == cond && thread){
            set_thread_state(thread, THREAD_READY);
            thread->wait_cond = NULL;
            tlinsert(&ready_list, thread);// the threads are in the READY state after waking up
            }
        }
    // The function does not return
    restore_interrupts(pie);
    // FIXME your code goes here
}

// INTERNAL FUNCTION DEFINITIONS
//

void init_main_thread(void) {
    // Initialize stack anchor with pointer to self
    main_thread.stack_anchor->ktp = &main_thread;
}

void init_idle_thread(void) {
    // Initialize stack anchor with pointer to self
    idle_thread.stack_anchor->ktp = &idle_thread;
}

static void set_running_thread(struct thread * thr) {
    asm inline ("mv tp, %0" :: "r"(thr) : "tp");
}

const char * thread_state_name(enum thread_state state) {
    static const char * const names[] = {
        [THREAD_UNINITIALIZED] = "UNINITIALIZED",
        [THREAD_WAITING] = "WAITING",
        [THREAD_SELF] = "SELF",
        [THREAD_READY] = "READY",
        [THREAD_EXITED] = "EXITED"
    };

    if (0 <= (int)state && (int)state < sizeof(names)/sizeof(names[0]))
        return names[state];
    else
        return "UNDEFINED";
};

void thread_reclaim(int tid) {
    struct thread * const thr = thrtab[tid];
    int ctid;

    assert (0 < tid && tid < NTHR && thr != NULL);
    assert (thr->state == THREAD_EXITED);

    // Make our parent thread the parent of our child threads. We need to scan
    // all threads to find our children. We could keep a list of all of a
    // thread's children to make this operation more efficient.

    for (ctid = 1; ctid < NTHR; ctid++) {
        if (thrtab[ctid] != NULL && thrtab[ctid]->parent == thr)
            thrtab[ctid]->parent = thr->parent;
    }

    thrtab[tid] = NULL;
    kfree(thr);
}

struct thread * create_thread(const char * name) {
    struct thread_stack_anchor * anchor;
    void * stack_page;
    struct thread * thr;
    int tid;

    trace("%s(name=\"%s\") in <%s:%d>", __func__, name, TP->name, TP->id);

    // Find a free thread slot.

    tid = 0;
    while (++tid < NTHR)
        if (thrtab[tid] == NULL)
            break;
    
    if (tid == NTHR)
        return NULL;
    
    // Allocate a struct thread and a stack

    thr = kcalloc(1, sizeof(struct thread));
    
    stack_page = kmalloc(STACK_SIZE);
    anchor = stack_page + STACK_SIZE;
    anchor -= 1; // anchor is at base of stack
    thr->stack_lowest = stack_page;
    thr->stack_anchor = anchor;
    anchor->ktp = thr;
    anchor->kgp = NULL;

    thrtab[tid] = thr;

    thr->id = tid;
    thr->name = name;
    thr->parent = TP;
    thr->lock_list = NULL;
    return thr;
}
//input: none
//output: none
//description: suspends the currently running thread and resumes the next thread on the ready-to-run list. The function does not return until the current thread is next scheduled for execution. If the current thread is TP, it is marked READY and placed on the ready-to-run list. The function reclaims the stack of the exited thread if it has exited. The function does not return.
//side effects: suspends the currently running thread, resumes the next thread on the ready-to-run list, marks the current thread as READY and places it on the ready-to-run list, reclaims the stack of the exited thread if it has exited
void running_thread_suspend(void) {
    struct thread *next_thread;
    int pie;
    
    trace("%s() in <%s:%d>", __func__, TP->name, TP->id);
    pie = disable_interrupts();
    // Mark current thread as READY and place it on the ready-to-run list
    if (TP->state == THREAD_SELF) {
    set_thread_state(TP, THREAD_READY);
    tlinsert(&ready_list, TP);
    }
    
    // Get next thread to run, with safety checks
    if (ready_list.head != NULL) {
        struct thread *thr = ready_list.head;
    
    // Validate the thread pointer before using it
        if (thr == NULL || (uintptr_t)thr < 0x1000) {
    // Invalid pointer, use idle thread
            next_thread = &idle_thread;
            ready_list.head = NULL;
            ready_list.tail = NULL;
            } else {
    // Valid pointer, remove from ready list
            ready_list.head = thr->list_next;
            if (ready_list.head == NULL)
            ready_list.tail = NULL;
            thr->list_next = NULL;
            next_thread = thr;
        }
        } else {
        next_thread = &idle_thread;
        }
        set_thread_state(next_thread, THREAD_SELF);
        restore_interrupts(pie);
        _thread_swtch(next_thread);
    
    if (TP->state == THREAD_EXITED) {
        if (TP->stack_lowest) {
            kfree(TP->stack_lowest);
            TP->stack_lowest = NULL;
            }
        }
    }

void tlclear(struct thread_list * list) {
    list->head = NULL;
    list->tail = NULL;
}

int tlempty(const struct thread_list * list) {
    return (list->head == NULL);
}

void tlinsert(struct thread_list * list, struct thread * thr) {
    thr->list_next = NULL;

    if (thr == NULL)
        return;

    if (list->tail != NULL) {
        assert (list->head != NULL);
        list->tail->list_next = thr;
    } else {
        assert(list->head == NULL);
        list->head = thr;
    }

    list->tail = thr;
}

struct thread * tlremove(struct thread_list * list) {
    struct thread * thr;

    thr = list->head;
    
    if (thr == NULL)
        return NULL;

    list->head = thr->list_next;
    
    if (list->head != NULL)
        thr->list_next = NULL;
    else
        list->tail = NULL;

    thr->list_next = NULL;
    return thr;
}

// Appends elements of l1 to the end of l0 and clears l1.

void tlappend(struct thread_list * l0, struct thread_list * l1) {
    if (l0->head != NULL) {
        assert(l0->tail != NULL);
        
        if (l1->head != NULL) {
            assert(l1->tail != NULL);
            l0->tail->list_next = l1->head;
            l0->tail = l1->tail;
        }
    } else {
        assert(l0->tail == NULL);
        l0->head = l1->head;
        l0->tail = l1->tail;
    }

    l1->head = NULL;
    l1->tail = NULL;
}

void idle_thread_func(void) {
    // The idle thread sleeps using wfi if the ready list is empty. Note that we
    // need to disable interrupts before checking if the thread list is empty to
    // avoid a race condition where an ISR marks a thread ready to run between
    // the call to tlempty() and the wfi instruction.

    for (;;) {
        // If there are runnable threads, yield to them.

        while (!tlempty(&ready_list))
            thread_yield();
        
        // No runnable threads. Sleep using the wfi instruction. Note that we
        // need to disable interrupts and check the runnable thread list one
        // more time (make sure it is empty) to avoid a race condition where an
        // ISR marks a thread ready before we call the wfi instruction.

        disable_interrupts();
        if (tlempty(&ready_list))
            asm ("wfi");
        enable_interrupts();
    }
}


void lock_init(struct lock *lock) {
    lock->locked = 0;
    lock->owner = NULL;
    lock->next = NULL;
    condition_init(&lock->wait_cond, "lock_wait_cond");
}

void lock_acquire(struct lock *lock) {
    int pie;
    
    pie = disable_interrupts();
    
    while (lock->locked) {
        // Lock is held by another thread, wait for it
        if (lock->owner == TP) {
            // Deadlock - trying to acquire a lock we already own
            restore_interrupts(pie);
            panic("Deadlock detected: thread trying to acquire a lock it already owns");
        }
        
        // Wait for the lock to be released
        condition_wait(&lock->wait_cond);
    }
    
    // Acquire the lock
    lock->locked = 1;
    lock->owner = TP;
    
    // Add to thread's lock list
    if (TP->lock_list == NULL) {
        TP->lock_list = lock;
    } else {
        lock->next = TP->lock_list;
        TP->lock_list = lock;
    }
    
    restore_interrupts(pie);
}

void lock_release(struct lock *lock) {
    struct lock **prev;
    int pie;
    
    pie = disable_interrupts();
    
    // Verify this thread owns the lock
    if (lock->owner != TP) {
        restore_interrupts(pie);
        return;
    }
    
    // Remove from thread's lock list
    prev = &TP->lock_list;
    while (*prev && *prev != lock) {
        prev = &(*prev)->next;
    }
    
    if (*prev) {
        *prev = lock->next;
    }
    
    // Release the lock
    lock->locked = 0;
    lock->owner = NULL;
    lock->next = NULL;
    
    // Wake up waiting threads
    condition_broadcast(&lock->wait_cond);
    
    restore_interrupts(pie);
}
