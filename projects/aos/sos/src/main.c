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
#include <autoconf.h>
#include <utils/util.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#include <cspace/cspace.h>
#include <aos/sel4_zf_logif.h>
#include <aos/debug.h>

#include <clock/clock.h>
#include <cpio/cpio.h>
#include <elf/elf.h>
#include <networkconsole/networkconsole.h>

#include <sel4runtime.h>
#include <sel4runtime/auxv.h>

#include "bootstrap.h"
#include "irq.h"
#include "network.h"
#include "frame_table.h"
#include "drivers/uart.h"
#include "ut.h"
#include "vmem_layout.h"
#include "mapping.h"
#include "elfload.h"
#include "syscalls.h"
#include "tests.h"
#include "utils.h"
#include "threads.h"
#include <sos/gen_config.h>
#include <utils/sglib.h>
#include <utils/list.h>
#include <sossharedapi/syscalls.h>
#include "user_process.h"
#include "pagetable.h"
#include "vm_region.h"
#include <nfsc/libnfs.h>
#include "fcntl.h"
#include "page_swap.h"
#include "cap_utils.h"
#include "syscall_handler/syscall_handler.h"
#include "fault_handler/fault_handler.h"
#include "recursive_mutex.h"
#ifdef CONFIG_SOS_GDB_ENABLED
#include "debugger.h"
#endif /* CONFIG_SOS_GDB_ENABLED */

#include <aos/vsyscall.h>
#include "backtrace.h"
#include <nfsc/libnfs.h>
#include "page_swap.h"
#include "user_app.h"
#include "sched_ctrl.h"

/*
 * To differentiate between signals from notification objects and and IPC messages,
 * we assign a badge to the notification object. The badge that we receive will
 * be the bitwise 'OR' of the notification object badge and the badges
 * of all pending IPC messages.
 *
 * All badged IRQs set high bit, then we use unique bits to
 * distinguish interrupt sources.
 */
#define IRQ_EP_BADGE         BIT(seL4_BadgeBits - 1ul)
#define IRQ_IDENT_BADGE_BITS MASK(seL4_BadgeBits - 1ul)

/* The linker will link this symbol to the start address  *
 * of an archive of attached applications.                */
extern char _cpio_archive[];
extern char _cpio_archive_end[];
extern char __eh_frame_start[];
/* provided by gcc */
extern void (__register_frame)(void *);

/* root tasks cspace */
cspace_t cspace;

seL4_CPtr sched_ctrl_start;
seL4_CPtr sched_ctrl_end;

struct syscall_loop_args {
    seL4_CPtr ep;
    seL4_CPtr reply;
};

struct network_console *network_console;

void write_to_buf(UNUSED struct network_console *network_console, char c) {
    bool is_empty_before = sglib_nwcs_input_t_is_empty(&nwcs_input);
    sglib_nwcs_input_t_add(&nwcs_input, c);

    sync_recursive_mutex_lock(nwcs_reader_mutex);
    if (is_empty_before && nwcs_reader != -1) {
        seL4_Signal(worker_threads[nwcs_reader]->ntfn);
    }
    sync_recursive_mutex_unlock(nwcs_reader_mutex);
}

NORETURN void syscall_loop(void* arg)
{
    seL4_CPtr reply = ((struct syscall_loop_args*)arg)->reply;    
    seL4_CPtr ep = ((struct syscall_loop_args*)arg)->ep;

    bool have_reply = false;
    seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0, 0, 0, 0);

    while (1) {
        seL4_Word badge = 0;
        seL4_MessageInfo_t message;
        /* Reply (if there is a reply) and block on ep, waiting for an IPC
         * sent over ep, or a notification from our bound notification object */
        if (have_reply) {
            message = seL4_ReplyRecv(ep, reply_msg, &badge, reply);
        } else {
            message = seL4_Recv(ep, &badge, reply);
        }
        /* Awake! We got a message - check the label and badge to
         * see what the message is about */
        seL4_Word label = seL4_MessageInfo_get_label(message);
        if (badge & IRQ_EP_BADGE) {
            /* It's a notification from our bound notification
             * object! */
            sos_handle_irq_notification(&badge, &have_reply);
        } else if (label == seL4_Fault_NullFault) {
            /* It's not a fault or an interrupt, it must be an IPC
             * message from console_test! */
            reply_msg = handle_syscall(badge, seL4_MessageInfo_get_length(message) - 1, &have_reply);
        } else {
            /* Handle the fault */
            reply_msg = handle_fault(message, &have_reply, user_processes[current_thread->assigned_pid]->command);
        }
    }
}

bool start_first_process(sos_thread_t *assigned_worker_thread, char *app_name)
{    
    elf_t elf_file = {};
    /* parse the cpio image */
    unsigned long elf_size;
    size_t cpio_len = _cpio_archive_end - _cpio_archive;
    const char *elf_base = cpio_get_file(_cpio_archive, cpio_len, app_name, &elf_size);
    if (elf_base == NULL) {
        ZF_LOGE("Unable to locate cpio header for %s", app_name);
        return false;
    }

    /* Ensure that the file is an elf file. */
    if (elf_newFile(elf_base, elf_size, &elf_file)) {
        ZF_LOGE("Invalid elf file");
        return false;
    }

    return create_process(assigned_worker_thread, app_name, assigned_worker_thread->assigned_pid, &elf_file, false);
}

/* Allocate an endpoint and a notification object for sos.
 * Note that these objects will never be freed, so we do not
 * track the allocated ut objects anywhere
 */
static void sos_ipc_init(seL4_CPtr *ipc_ep, seL4_CPtr *ntfn)
{
    /* Create an notification object for interrupts */
    ut_t *ut = alloc_retype(ntfn, seL4_NotificationObject, seL4_NotificationBits);
    ZF_LOGF_IF(!ut, "No memory for notification object");

    /* Bind the notification object to our TCB */
    seL4_Error err = seL4_TCB_BindNotification(seL4_CapInitThreadTCB, *ntfn);
    ZF_LOGF_IFERR(err, "Failed to bind notification object to TCB");

    /* Create an endpoint for user application IPC */
    ut = alloc_retype(ipc_ep, seL4_EndpointObject, seL4_EndpointBits);
    ZF_LOGF_IF(!ut, "No memory for endpoint");
}

/* called by crt */
seL4_CPtr get_seL4_CapInitThreadTCB(void)
{
    return seL4_CapInitThreadTCB;
}

/* tell muslc about our "syscalls", which will be called by muslc on invocations to the c library */
void init_muslc(void)
{
    setbuf(stdout, NULL);

    muslcsys_install_syscall(__NR_set_tid_address, sys_set_tid_address);
    muslcsys_install_syscall(__NR_writev, sys_writev);
    muslcsys_install_syscall(__NR_exit, sys_exit);
    muslcsys_install_syscall(__NR_rt_sigprocmask, sys_rt_sigprocmask);
    muslcsys_install_syscall(__NR_gettid, sys_gettid);
    muslcsys_install_syscall(__NR_getpid, sys_getpid);
    muslcsys_install_syscall(__NR_tgkill, sys_tgkill);
    muslcsys_install_syscall(__NR_tkill, sys_tkill);
    muslcsys_install_syscall(__NR_exit_group, sys_exit_group);
    muslcsys_install_syscall(__NR_ioctl, sys_ioctl);
    muslcsys_install_syscall(__NR_mmap, sys_mmap);
    muslcsys_install_syscall(__NR_brk,  sys_brk);
    muslcsys_install_syscall(__NR_clock_gettime, sys_clock_gettime);
    muslcsys_install_syscall(__NR_nanosleep, sys_nanosleep);
    muslcsys_install_syscall(__NR_getuid, sys_getuid);
    muslcsys_install_syscall(__NR_getgid, sys_getgid);
    muslcsys_install_syscall(__NR_openat, sys_openat);
    muslcsys_install_syscall(__NR_close, sys_close);
    muslcsys_install_syscall(__NR_socket, sys_socket);
    muslcsys_install_syscall(__NR_bind, sys_bind);
    muslcsys_install_syscall(__NR_listen, sys_listen);
    muslcsys_install_syscall(__NR_connect, sys_connect);
    muslcsys_install_syscall(__NR_accept, sys_accept);
    muslcsys_install_syscall(__NR_sendto, sys_sendto);
    muslcsys_install_syscall(__NR_recvfrom, sys_recvfrom);
    muslcsys_install_syscall(__NR_readv, sys_readv);
    muslcsys_install_syscall(__NR_getsockname, sys_getsockname);
    muslcsys_install_syscall(__NR_getpeername, sys_getpeername);
    muslcsys_install_syscall(__NR_fcntl, sys_fcntl);
    muslcsys_install_syscall(__NR_setsockopt, sys_setsockopt);
    muslcsys_install_syscall(__NR_getsockopt, sys_getsockopt);
    muslcsys_install_syscall(__NR_ppoll, sys_ppoll);
    muslcsys_install_syscall(__NR_madvise, sys_madvise);
}

void nfs_call_loop(seL4_CPtr ep, bool *condition_on_wait) {
    /* Create reply object */
    seL4_CPtr reply;
    ut_t *reply_ut = create_cap(&reply, seL4_ReplyObject, seL4_ReplyBits);
                          
    UNUSED bool have_reply = false;
    while (!(*condition_on_wait)) { // waits for nfs_mount
        seL4_Word badge = 0;
        seL4_Recv(ep, &badge, reply);
        if (badge & IRQ_EP_BADGE) {
            /* It's a notification from our bound notification
             * object! */
            sos_handle_irq_notification(&badge, &have_reply);
        }
    }

    // free reply object
    free_cap(reply_ut, reply);
}

void start_first_process_then_loop(void *arg) {
    /* Start user process */
    ZF_LOGI("Start first process\n");
    bool success = start_first_process(worker_threads[SOS_BOOTSTRAP_THREAD_ID], APP_NAME);
    ZF_LOGF_IF(!success, "Failed to start first process");
    syscall_loop(arg);
}

sos_thread_t* create_worker_thread(size_t thread_id, thread_main_f *function, bool start_execution) {
    /* Create a notification object */
    seL4_CPtr thread_ntfn;
    ut_t *ut = create_cap(&thread_ntfn, seL4_NotificationObject, seL4_NotificationBits);
    ZF_LOGF_IF(!ut, "Failed to create thread_ntfn cap");

    /* Start the worker thread */
    struct syscall_loop_args *worker_sys_loop_args = malloc(sizeof(struct syscall_loop_args));

    sos_thread_t* thread = thread_create(thread_id, function, worker_sys_loop_args, thread_id + 1, false, seL4_MinPrio, thread_ntfn, true);
    ZF_LOGF_IF(!thread, "Failed to create worker thread");

    /* worker thread's IPC EP is created within the `thread_create` function */
    worker_sys_loop_args->ep = thread->ipc_ep;
    worker_sys_loop_args->reply = thread->reply;

    /* start execution */
    if (start_execution) {
        thread_resume(thread);
    }

    /* store the worker thread */
    worker_threads[thread_id] = thread;
    
    /* currently not assigned to any process */
    worker_threads[thread_id]->assigned_pid = -1;

    return thread;
}


NORETURN void *main_continued(UNUSED void *arg)
{
    /* Initialise other system components here */
    seL4_CPtr ipc_ep, ntfn;
    sos_ipc_init(&ipc_ep, &ntfn);
    sos_init_irq_dispatch(
        &cspace,
        seL4_CapIRQControl,
        ntfn,
        IRQ_EP_BADGE,
        IRQ_IDENT_BADGE_BITS
    );

    /* Initialize threads library */
#ifdef CONFIG_SOS_GDB_ENABLED
    /* Create an endpoint that the GDB threads listens to */
    seL4_CPtr gdb_recv_ep;
    ut_t *ep_ut = alloc_retype(&gdb_recv_ep, seL4_EndpointObject, seL4_EndpointBits);
    ZF_LOGF_IF(ep_ut == NULL, "Failed to create GDB endpoint");

    init_threads(ipc_ep, gdb_recv_ep, sched_ctrl_start, sched_ctrl_end);
#else
    init_threads(ipc_ep, ipc_ep, sched_ctrl_start, sched_ctrl_end);
#endif /* CONFIG_SOS_GDB_ENABLED */

    frame_table_init(&cspace, seL4_CapInitThreadVSpace);

    /* run sos initialisation tests */
    run_tests(&cspace);

    /* Map the timer device (NOTE: this is the same mapping you will use for your timer driver -
     * sos uses the watchdog timers on this page to implement reset infrastructure & network ticks,
     * so touching the watchdog timers here is not recommended!) */
    void *timer_vaddr = sos_map_device(&cspace, PAGE_ALIGN_4K(TIMER_MAP_BASE), PAGE_SIZE_4K);

    /* Initialise the network hardware. */
    ZF_LOGI("Network init\n");
    
    network_init(&cspace, timer_vaddr, ntfn);
    nfs_call_loop(ipc_ep, &has_init_network);

    network_console = network_console_init();

    /* Initialize network console buffer */
    // SGLIB_QUEUE_INIT(char, nwcs_buf, i, j);
    network_console_register_handler(network_console, write_to_buf);
    nwcs_reader_mutex = malloc(sizeof(sync_recursive_mutex_t));
    sync_recursive_mutex_new(nwcs_reader_mutex);
    
    /* Init page swap */
    init_page_swap();
    nfs_call_loop(ipc_ep, &has_init_page_swap);


#ifdef CONFIG_SOS_GDB_ENABLED
    /* Initialize the debugger */
    seL4_Error debug_err = debugger_init(DEBUGGER_THREAD_ID, &cspace, seL4_CapIRQControl, gdb_recv_ep);
    ZF_LOGF_IF(debug_err, "Failed to initialize debugger %d", debug_err);
    char secret_string[15] = "Welcome to AOS!";
#endif /* CONFIG_SOS_GDB_ENABLED */

    /* Initialises the timer */
    ZF_LOGI("Timer init\n");
    start_timer(timer_vaddr);

    /* Register an IRQ handler for the timer here. See "irq.h". */
    seL4_Word irq_number = meson_timeout_irq(MESON_TIMER_A);
    bool edge_triggered = true;
    seL4_IRQHandler irq_handler = 0;

    int init_irq_err = sos_register_irq_handler(irq_number, edge_triggered, timer_irq, NULL, &irq_handler);
    ZF_LOGF_IF(init_irq_err != 0, "Failed to initialise IRQ");
    seL4_IRQHandler_Ack(irq_handler);

    /* Initialize free PIDs queue */
    init_free_pids();

    /*  Create a thread pool */
    for (size_t id = 1; id < MAX_WORKER_THREADS; ++id) {
        create_worker_thread(id, syscall_loop, true);
    }
    
    /* Initialize a mutex for thread pool */
    worker_threads_mutex = malloc(sizeof(sync_recursive_mutex_t));
    sync_recursive_mutex_new(worker_threads_mutex);
    
    /* Create a worker thread for handling interrupts */
    seL4_CPtr interrupt_thread_reply;
    create_cap(&interrupt_thread_reply, seL4_ReplyObject, seL4_ReplyBits);

    struct syscall_loop_args interrupts_handler_args = { .ep = ipc_ep, .reply = interrupt_thread_reply };
    
    // unbinds ntfn from init thread TCB, because we're going to bind ntfn to the interrupts handler thread
    seL4_Error err = seL4_TCB_UnbindNotification(seL4_CapInitThreadTCB);
    ZF_LOGF_IF(err != seL4_NoError, "Failed to unbind notification from init thread TCB, seL4_Error=%d", err);
    
    sos_thread_t* interrupt_thread = thread_create(SOS_INTERRUPT_THREAD_ID, syscall_loop, &interrupts_handler_args, SOS_INTERRUPT_THREAD_ID + 1, true, seL4_MaxPrio, ntfn, true);
    ZF_LOGF_IF(!interrupt_thread, "Failed to create interrupt thread");

    /* init user_processes_mutex */
    user_processes_mutex = malloc(sizeof(sync_recursive_mutex_t));
    ZF_LOGF_IF(!user_processes_mutex, "Failed to create user processes mutex");
    sync_recursive_mutex_new(user_processes_mutex);

    /* Create the bootstrap thread. It will load the first user process, then transition into the syscall loop. */
    sos_thread_t *boostrap_thread = create_worker_thread(SOS_BOOTSTRAP_THREAD_ID, start_first_process_then_loop, false);
    
    /* Assigned PID to the boostrap thread */
    int available_pid = get_available_pid();
    ZF_LOGF_IF(available_pid == -1, "Failed to get a valid available pid");
    boostrap_thread->assigned_pid = available_pid;

    /* Resume the bootstrap thread so it can begin loading the initial user process. */
    thread_resume(boostrap_thread);
    
    /* Enter the syscall loop on the main thread to keep SOS running */
    seL4_CPtr main_thread_ipc_ep;
    create_cap(&main_thread_ipc_ep, seL4_EndpointObject, seL4_EndpointBits);
    
    seL4_CPtr main_thread_reply;
    create_cap(&main_thread_reply, seL4_ReplyObject, seL4_ReplyBits);
    
    struct syscall_loop_args main_thread_args = { .ep = main_thread_ipc_ep, .reply = main_thread_reply };
    syscall_loop(&main_thread_args);
}
/*
 * Main entry point - called by crt.
 */
int main(void)
{
    init_muslc();

    /* register the location of the unwind_tables -- this is required for
     * backtrace() to work */
    __register_frame(&__eh_frame_start);

    seL4_BootInfo *boot_info = sel4runtime_bootinfo();

    debug_print_bootinfo(boot_info);

    ZF_LOGI("\nSOS Starting...\n");

    NAME_THREAD(seL4_CapInitThreadTCB, "SOS:root");

    sched_ctrl_start = boot_info->schedcontrol.start;
    sched_ctrl_end = boot_info->schedcontrol.end;

    /* Initialise the cspace manager, ut manager and dma */
    sos_bootstrap(&cspace, boot_info);

    /* switch to the real uart to output (rather than seL4_DebugPutChar, which only works if the
     * kernel is built with support for printing, and is much slower, as each character print
     * goes via the kernel)
     *
     * NOTE we share this uart with the kernel when the kernel is in debug mode. */
    uart_init(&cspace);
    update_vputchar(uart_putchar);

    /* test print */
    ZF_LOGI("SOS Started!\n");

    /* allocate a bigger stack and switch to it -- we'll also have a guard page, which makes it much
     * easier to detect stack overruns */
    seL4_Word vaddr = SOS_STACK;
    for (int i = 0; i < SOS_STACK_PAGES; i++) {
        seL4_CPtr frame_cap;
        ut_t *frame = alloc_retype(&frame_cap, seL4_ARM_SmallPageObject, seL4_PageBits);
        ZF_LOGF_IF(frame == NULL, "Failed to allocate stack page");
        seL4_Error err = map_frame(&cspace, frame_cap, seL4_CapInitThreadVSpace,
                                   vaddr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
        ZF_LOGF_IFERR(err, "Failed to map stack");
        vaddr += PAGE_SIZE_4K;
    }

    utils_run_on_stack((void *) vaddr, main_continued, NULL);

    UNREACHABLE();
}


