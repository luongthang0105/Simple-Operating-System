/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */
#pragma once

#include <sel4runtime.h>
#include <threads.h>
#include <cspace/cspace.h>
#include "user_process.h"
#include "ut.h"
#include <sos/gen_config.h>

extern cspace_t cspace;

typedef struct {
    sos_pid_t assigned_pid;
    uint32_t thread_id;
    
    ut_t *tcb_ut;
    seL4_CPtr tcb;

    seL4_CPtr ntfn;

    ut_t *ipc_ep_ut;
    seL4_CPtr ipc_ep;   // endpoint for other threads to communicate with this thread via IPC
    seL4_CPtr user_ep;  // endpoint to communicate with SOS main thread, with a badge on it
    seL4_CPtr fault_ep;
    ut_t *ipc_buffer_ut;
    seL4_CPtr ipc_buffer;
    seL4_Word ipc_buffer_vaddr;

    ut_t *sched_context_ut;
    seL4_CPtr sched_context;

    ut_t *stack_ut;
    seL4_CPtr stack;
    seL4_Word badge;

    uintptr_t tls_base;
} sos_thread_t;

typedef void thread_main_f(void *);

#define MAX_WORKER_THREADS      16
#define SOS_BOOTSTRAP_THREAD_ID 0
#define SOS_INTERRUPT_THREAD_ID (MAX_WORKER_THREADS)
#ifdef CONFIG_SOS_GDB_ENABLED
#define DEBUGGER_THREAD_ID      (MAX_WORKER_THREADS + 1)
#endif /* CONFIG_SOS_GDB_ENABLED */

extern __thread sos_thread_t *current_thread;
extern sos_thread_t *worker_threads[MAX_WORKER_THREADS];

void init_threads(seL4_CPtr ipc_ep, seL4_CPtr fault_ep, seL4_CPtr sched_ctrl_start_, seL4_CPtr sched_ctrl_end_);
sos_thread_t *spawn(size_t thread_id, thread_main_f function, void *arg, seL4_Word badge, bool debugger_add);
sos_thread_t *debugger_spawn(size_t thread_id, thread_main_f function, void *arg, seL4_Word badge, seL4_CPtr bound_ntfn);
sos_thread_t *thread_create(size_t thread_id, thread_main_f function, void *arg, seL4_Word badge, bool resume, seL4_Word prio, 
                            seL4_CPtr bound_ntfn, bool debugger_add);
int thread_suspend(sos_thread_t *thread);
int thread_resume(sos_thread_t *thread);
