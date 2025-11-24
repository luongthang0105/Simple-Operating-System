#include "user_process.h"
#include "pagetable.h"
#include "threads.h"
#include "cap_utils.h"
#include <clock/clock.h>

extern cspace_t cspace;

user_process_t *user_processes[MAX_NUM_PROCESSES] = {NULL};
SGLIB_DEFINE_QUEUE_FUNCTIONS(pid_queue_t, pid_free_record_t, arr, i, j, MAX_NUM_PROCESSES + 1);
pid_queue_t free_pids = {.arr = {0}, .i = 0, .j = 0};

void init_free_pids() {
    for (sos_pid_t pid = 0; pid < MAX_NUM_PROCESSES; pid++) {
        /*  let timestamp be 0 initially, so that it would always be available to use at first. */
        pid_free_record_t record = { .pid = pid, .freed_timestamp = 0 };
        sglib_pid_queue_t_add(&free_pids, record);
    }
    // initialise the mutex
    free_pids_mutex = malloc(sizeof(sync_mutex_t));
    sync_mutex_new(free_pids_mutex);
}

int delete_user_process(int pid) {
    printf("delete user_process\n");
    if (pid < 0 || pid >= MAX_NUM_PROCESSES) return -1;

    user_process_t *user_process = user_processes[pid];
    if (user_process == NULL) return -1;

    /* First, suspend the thread so our subsequent destruction steps don't cause any fault/unexpected behaviour. */
    seL4_Error err = seL4_TCB_Suspend(user_process->tcb);
    if (err != seL4_NoError) {
        ZF_LOGE("Failed to suspend user process, seL4_Error=%d", err);
        return -1;
    }

    /* vfs */
    destroy_vfs(user_process->vfs);
    printf("delete vfs\n");

    /* linked list of frames - page global directory  */
    destroy_pgd(user_process->page_global_directory, &cspace);
    printf("delete pgd\n");

    /* vm_regions */
    destroy_vm_regions(user_process->vm_regions);
    printf("delete vm_region\n");

    /* IPC buffer has already been freed from `destroy_pgd`. */ 

    /* user_ep */
    free_cap(NULL, user_process->user_ep);
    /* TCB object */
    free_cap(user_process->tcb_ut, user_process->tcb);
    printf("delete tcb\n");

    /* stack has already been freed from `destroy_pgd`. */

    /* add the pid back to the free_pids queue to reuse it */
    pid_free_record_t pid_free_record = { .pid = pid, .freed_timestamp = get_time()};
    sglib_pid_queue_t_add(&free_pids, pid_free_record);
    printf("reuse pid\n");

    /* vspace */
    // TODO: unassigned vspace from an asid pool. currently could not find a function for this
    free_cap(user_process->vspace_ut, user_process->vspace);
    printf("delete vspace\n");

    /* cspace */
    cspace_destroy(&user_process->cspace);

    free(user_process);
    user_processes[pid] = NULL;
    
    return 0;
}

#define PID_REUSE_COOLDOWN_US 1000

int get_available_pid() {
    sync_mutex_lock(free_pids_mutex);

    if (sglib_pid_queue_t_is_empty(&free_pids)) {
        printf("free pids queue is empty!!!!\n");
        sync_mutex_unlock(free_pids_mutex);
        return -1;
    }

    int result = -1;

    while (!sglib_pid_queue_t_is_empty(&free_pids)) {
        pid_free_record_t record = sglib_pid_queue_t_first_element(&free_pids);
        sglib_pid_queue_t_delete_first(&free_pids);
        if (get_time() - record.freed_timestamp >= PID_REUSE_COOLDOWN_US) {
            result = record.pid;
            break;
        } else {
            sglib_pid_queue_t_add(&free_pids, record);
        }
    }
    
    sync_mutex_unlock(free_pids_mutex);
    return result;
}

user_process_t *get_current_user_process_by_thread(uint64_t thread_id)
{
    sos_pid_t assigned_pid = worker_threads[thread_id]->assigned_pid;
    return user_processes[assigned_pid];
}
user_process_t *get_current_user_process()
{
    return get_current_user_process_by_thread(current_thread->thread_id);
}

int copy_from_user(void *to, const void *from, size_t nbyte)
{
    size_t rem_bytes = nbyte;
    size_t bytes_copied = 0;
    uintptr_t from_vaddr = (uintptr_t)from;

    user_process_t *user_process = get_current_user_process();

    char *temp = (char *)to;
    while (rem_bytes > 0)
    {
        page_metadata_t *page = find_page(from_vaddr, user_process->page_global_directory);
        if (!page)
        {
            ZF_LOGE("Unable to find a page for buf_vaddr at %p", (void *)from_vaddr);
            return -1;
        }

        // source data of the "from" buf
        unsigned char *source_data = frame_data(page->frame_ref);

        size_t offset = from_vaddr % PAGE_SIZE_4K;
        size_t max_bytes_to_copy = PAGE_SIZE_4K - offset;
        size_t bytes_to_copy = MIN(rem_bytes, max_bytes_to_copy);

        memcpy(&temp[bytes_copied], &source_data[offset], bytes_to_copy);

        rem_bytes -= bytes_to_copy;
        from_vaddr += bytes_to_copy;
        bytes_copied += bytes_to_copy;
    }
    return 0;
}

int copy_to_user(void *to, const void *from, size_t nbyte)
{
    size_t rem_bytes = nbyte;
    size_t bytes_copied = 0;
    uintptr_t to_vaddr = (uintptr_t)to;

    user_process_t *user_process = get_current_user_process();

    while (rem_bytes > 0)
    {
        page_metadata_t *page = find_page(to_vaddr, user_process->page_global_directory);
        if (!page)
        {
            ZF_LOGI("vaddr %p is not mapped to any frame, trying to allocate frame...", (void *)to_vaddr);
            vm_region_t *valid_region = find_valid_region(to_vaddr, BIT(6), user_process->vm_regions);

            if (valid_region == NULL)
            {
                ZF_LOGE("vaddr %p resolves to an invalid region access", (void *)to_vaddr);
                return -1;
            }

            int result = alloc_map_frame(&cspace, to_vaddr, user_process, valid_region->rights);
            if (result != 0)
            {
                ZF_LOGE("Unable to allocate a new frame at %p!\n", (void *)to_vaddr);
                return -1;
            }

            ZF_LOGI("Successfully allocate a new frame at %p", (void *)to_vaddr);
            continue;
        }

        // source data of the "to" buf
        unsigned char *source_data = frame_data(page->frame_ref);

        size_t offset = to_vaddr % PAGE_SIZE_4K;
        size_t max_bytes_to_copy = PAGE_SIZE_4K - offset;
        size_t bytes_to_copy = MIN(rem_bytes, max_bytes_to_copy);

        // char *temp = (char*) from;
        memcpy(source_data + offset, from + bytes_copied, bytes_to_copy);

        rem_bytes -= bytes_to_copy;
        to_vaddr += bytes_to_copy;
        bytes_copied += bytes_to_copy;
    }
    return 0;
}