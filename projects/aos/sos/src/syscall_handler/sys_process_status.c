#include "../user_process.h"
#include "sys_process_status.h"
#include <sossharedapi/process.h>

int handle_sos_process_status() {
    uintptr_t processes_vaddr = seL4_GetMR(1);
    unsigned max              = seL4_GetMR(2);

    sync_recursive_mutex_lock(user_processes_mutex);

    int process_count = MIN(get_num_active_processes(), max);
    sos_process_t *processes = malloc(sizeof(sos_process_t) * process_count);
    if (!processes) {
        ZF_LOGE("Failed to malloc array of process status");
        sync_recursive_mutex_unlock(user_processes_mutex);
        return 0;
    }

    get_user_process_status(processes, process_count);
    
    sync_recursive_mutex_unlock(user_processes_mutex);

    int status = copy_to_user((void *)processes_vaddr, (void *)processes, sizeof(sos_process_t) * process_count);
    
    free(processes);
    
    if (status == -1) return 0;
    return process_count;
}