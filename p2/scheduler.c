/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * scheduler.c
 */

#undef _FORTIFY_SOURCE

/**
 * Needs:
 *   setjmp()
 *   longjmp()
 */

/* research the above Needed API and design accordingly */

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include <stdlib.h>
#include "system.h"
#include "scheduler.h"

struct thread {
    jmp_buf ctx;
    enum {
        STATUS_, /* initial status */
        STATUS_RUNNING,
        STATUS_SLEEPING,
        STATUS_TERMINATED
    } status;
    scheduler_fnc_t function; /* thread start function*/
    void *args;                /* argument for the start function*/
    struct {
        void *memory_;
        void *memory;
    } stack;
    struct thread *link; 
};

static struct {
    struct thread *head;
    struct thread *thread; /* current running thread */
    jmp_buf ctx;
} state;

static void destroy(void) {
    struct thread *current = state.head;
    struct thread *start = state.head; 
    /*printf("Entering destroy function.\n");*/
    if (current) { 
        do {
            struct thread *next = current->link;
            free(current->stack.memory_);
            free(current);
            current = next;
        } while (current && current != start); 
    }
    state.head = NULL;
    state.thread = NULL;
    /*printf("All threads destroyed.\n");*/
}

static struct thread *thread_candidate(void) {
    struct thread *current;
    struct thread *start;

    /*printf("Looking for a thread candidate.\n");*/
    if (!state.head) {
        printf("No threads to run.\n");
        return NULL;
    }

    /* Start from next thread or head if no current thread.*/
    start = current = state.thread ? state.thread->link : state.head;

    do {
         if (current->status != STATUS_TERMINATED && (current->status == STATUS_ || current->status == STATUS_SLEEPING)) {
            /*printf("Thread candidate found.\n");*/
            return current;
        }
        current = current->link;
        if (!current) { /* If at the end of the list, wrap around.*/
            current = state.head;
        }
    } while (current != start); 

    return NULL;
}


int scheduler_create(scheduler_fnc_t fnc, void *arg) {
    struct thread *new_thread = (struct thread *)malloc(sizeof(struct thread));
    int stack_size;
    /*printf("Creating a new thread.\n");*/
    
    if (!new_thread) {
        printf("Failed to allocate memory for new thread.\n");
        return -1;
    }

    new_thread->status = STATUS_;
    new_thread->function = fnc;
    new_thread->args = arg;

    stack_size = page_size();
    new_thread->stack.memory_ = malloc(stack_size);
    if (!new_thread->stack.memory_) {
        printf("Failed to allocate memory for new thread's stack.\n");
        free(new_thread);
        return -1;
    }

    /* Align the memory to the required page size.*/
    new_thread->stack.memory = memory_align(new_thread->stack.memory_, stack_size);

    /* If no head exists, this is the first thread.*/
    if (!state.head) {
        state.head = new_thread;
        new_thread->link = new_thread; /* Point to itself to form a circular list.*/
    } 
    else {
        /* Attach new thread to the beginning, but adjust links to keep it circular.*/
        new_thread->link = state.head->link;
        state.head->link = new_thread;
        state.head = new_thread;
    }

    /*printf("Thread created successfully.\n");*/
    return 0;
}


static void schedule(void) {
    struct thread *candidate = thread_candidate();
    uint64_t rsp;
    /*printf("Scheduling next thread.\n");*/

    if (candidate) {

        state.thread = candidate;

        if (candidate->status == STATUS_) {
            /*printf("Starting a new thread.\n");*/
            candidate->status = STATUS_RUNNING;

            rsp = (uint64_t)candidate->stack.memory + page_size();
            __asm__ volatile("mov %[rs], %%rsp \n" : [rs] "+r" (rsp) : );
            candidate->function(candidate->args);
            
            candidate->status = STATUS_TERMINATED;
            /*printf("Thread terminated.\n");*/
            longjmp(state.ctx, 1);  
        } else {
            /*printf("Resuming a sleeping thread.\n");*/
            candidate->status = STATUS_RUNNING;
            longjmp(candidate->ctx, 1);
        }
    }
}


void scheduler_execute(void) {
    int jmp_val;
    printf("Executing scheduler.\n");
    
    jmp_val = setjmp(state.ctx);
    if (jmp_val == 0 || jmp_val == 1) {
        schedule();
    } 
    destroy();
}

void scheduler_yield() {
    struct thread *candidate;
    /*printf("Yielding current thread.\n");*/

    if (setjmp(state.thread->ctx) == 0) {
        
        candidate = thread_candidate();

        if (!candidate) {
            printf("No thread candidate to yield to.\n");
            return;
        }

        state.thread->status = STATUS_SLEEPING;
        /*printf("Current thread set to sleeping. Jumping back to scheduler_execute.\n");*/
        longjmp(state.ctx, 1);
    }
}
