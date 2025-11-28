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

#define THREAD_NAME_MAX_LEN     32
extern cspace_t cspace;

typedef uint32_t tid_t;
typedef void thread_main_f(void *);

typedef struct sos_thread {
    pid_t assigned_pid;
    tid_t thread_id;
    
    ut_t *tcb_ut;
    seL4_CPtr tcb;

    seL4_CPtr ntfn;

    ut_t *ipc_ep_ut;
    seL4_CPtr ipc_ep;   // endpoint for other threads to communicate with this thread via IPC
    
    ut_t *reply_ut;
    seL4_CPtr reply;   // endpoint for other threads to communicate with this thread via IPC

    seL4_CPtr user_ep;  // endpoint to communicate with SOS main thread, with a badge on it
    seL4_CPtr fault_ep;
    ut_t *ipc_buffer_ut;
    seL4_CPtr ipc_buffer;
    seL4_Word ipc_buffer_vaddr;

    ut_t *sched_context_ut;
    seL4_CPtr sched_context;

    ut_t *stack_ut;

    /* data for calling rerun_thread. These are copied while running `thread_create`. */
    seL4_CPtr base_stack_ptr;
    thread_main_f *start_func;
    void *start_func_arg;
    bool debugger_add;

    seL4_Word badge;

    uintptr_t tls_base;
} sos_thread_t;

#define MAX_WORKER_THREADS      16 /* worker threads will have IDs within [0, MAX_WORKER_THREADS - 1]*/
#define SOS_BOOTSTRAP_THREAD_ID 0 /* This worker thread is responsible for starting the first user process (i.e. `APP_NAME`) */
#define SOS_INTERRUPT_THREAD_ID (MAX_WORKER_THREADS)
#ifdef CONFIG_SOS_GDB_ENABLED
#define DEBUGGER_THREAD_ID      (MAX_WORKER_THREADS + 1)
#endif /* CONFIG_SOS_GDB_ENABLED */

extern __thread sos_thread_t *current_thread;
extern sos_thread_t *worker_threads[MAX_WORKER_THREADS];
extern sync_recursive_mutex_t* worker_threads_mutex;

void init_threads(seL4_CPtr ipc_ep, seL4_CPtr fault_ep, seL4_CPtr sched_ctrl_start_, seL4_CPtr sched_ctrl_end_);
sos_thread_t *get_available_worker_thread();
sos_thread_t *spawn(tid_t thread_id, thread_main_f function, void *arg, seL4_Word badge, bool debugger_add);
sos_thread_t *debugger_spawn(tid_t thread_id, thread_main_f function, void *arg, seL4_Word badge, seL4_CPtr bound_ntfn);
sos_thread_t *thread_create(tid_t thread_id, thread_main_f function, void *arg, seL4_Word badge, bool resume, seL4_Word prio, 
                            seL4_CPtr bound_ntfn, bool debugger_add);
int thread_suspend(sos_thread_t *thread);
int thread_resume(sos_thread_t *thread);
void worker_thread_rerun(sos_thread_t *thread);
void syscall_loop(void* arg);
