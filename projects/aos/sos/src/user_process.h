#pragma once
#include "ut.h"
#include "vm_region.h"
#include <nfsc/libnfs.h>
#include <sossharedapi/vfs.h>
#include <sossharedapi/process.h>
#include "recursive_mutex.h"
#include "waitlist.h"

typedef uint32_t tid_t;
struct page_global_directory;
typedef struct page_global_directory pgd_t;
struct user_process
{   
    tid_t assigned_worker_thread_id;
    unsigned  size;            /* in pages */
    unsigned  stime;           /* start time in msec since booting */
    char      command[N_NAME]; /* Name of executable */
    ut_t *tcb_ut;
    seL4_CPtr tcb;
    ut_t *vspace_ut;
    seL4_CPtr vspace;

    seL4_CPtr user_ep; // ep to communicate to SOS

    seL4_CPtr ipc_buffer;

    ut_t *sched_context_ut;
    seL4_CPtr sched_context;

    cspace_t cspace;

    seL4_CPtr stack;
    uintptr_t guard_page_vaddr;

    pgd_t *page_global_directory;
    list_t *vm_regions;

    vm_region_t *heap_region;
    vm_region_t *stack_region;

    /** A linkedlist of notification cap whose worker thread is waiting on this user process to exit. 
     *  On destruction of this process, all notification caps in this linkedlist will be signaled.
    */
    waitlist_t *waitlist;

    // filesystem
    /* main thread running the callback will assign a (struct nfsdir*) to this variable,
    so worker thread can use this*/
    struct nfsdir *curr_dir;

    vfs_t *vfs;
};
typedef struct user_process user_process_t;

#define MAX_NUM_PROCESSES 16
#define PROCESSES_POOL_SZ (MAX_NUM_PROCESSES * 2)
extern user_process_t *user_processes[PROCESSES_POOL_SZ];
extern sync_recursive_mutex_t *user_processes_mutex;

/*  Represents a PID that has been freed and the timestamp at which it became
    available for reuse. Entries of this type are stored in a queue so the
    allocator can delay reusing the PID until enough time has passed to
    avoid race conditions.
*/
typedef struct
{
    pid_t pid;
    uint64_t freed_timestamp;
} pid_free_record_t;

/*  A queue that contains the currently free process id
    It should always have at least one item in it, until we reach MAX_NUM_PROCESSES processes
    The initial items in the queue are process ids from 0 to MAX_NUM_PROCESSES.
*/
typedef struct pid_queue
{
    pid_free_record_t arr[PROCESSES_POOL_SZ + 1];
    size_t i;
    size_t j;
} pid_queue_t;

extern pid_queue_t free_pids;
sync_recursive_mutex_t *free_pids_mutex;

/** Copy data from SOS to user app.
 *  @returns 0 if `nbyte` was successfully copied, -1 otherwise.
 */
int copy_to_user(void *to, const void *from, size_t nbyte);

/**
 * Copy data from user app to SOS.
 * @returns 0 if `nbyte` was successfully copied, -1 otherwise.
 */
int copy_from_user(void *to, const void *from, size_t nbyte);

// TODO: add comment
user_process_t *get_current_user_process();
user_process_t *get_current_user_process_by_thread(uint64_t thread_id);
void init_free_pids();
/**
 * Returns an available pid.
 * 
 * @returns If an available pid does not exist, returns -1. Otherwise, returns the available pid.
 */
int get_available_pid();
int delete_user_process(int pid);
int get_num_active_processes();
void get_user_process_status(sos_process_t *processes, int num_active_processes);