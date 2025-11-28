#include "../user_process.h"
#include "../threads.h"
#include "../cap_utils.h"
#include "../waitlist.h"

int handle_sos_process_wait() {
    int pid_to_wait = seL4_GetMR(1);

    sync_recursive_mutex_lock(user_processes_mutex);
    if (pid_to_wait == -1) {
        /* choose a live process */
        for (int pid = 0; pid < PROCESSES_POOL_SZ; ++pid) {
            if (pid != current_thread->assigned_pid && user_processes[pid] != NULL) {
                pid_to_wait = pid;
                break;
            }
        }

        if (pid_to_wait == -1) {
            sync_recursive_mutex_unlock(user_processes_mutex);
            return -1;
        }
    } else if (user_processes[pid_to_wait] == NULL) {
        ZF_LOGE("Does not exist a user process with given pid = %d", pid_to_wait);
        sync_recursive_mutex_unlock(user_processes_mutex);
        return -1;
    }

    /* gets the waitlist */

    if (add_waiter(user_processes[pid_to_wait]->waitlist, current_thread->ipc_ep)) {
        sync_recursive_mutex_unlock(user_processes_mutex);
        return -1;
    }

    sync_recursive_mutex_unlock(user_processes_mutex);
    seL4_Wait(current_thread->ipc_ep, NULL);

    return pid_to_wait;
}