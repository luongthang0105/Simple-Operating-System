#include "../user_process.h"
#include "sys_process_delete.h"

int handle_sos_process_delete() {
    int pid = seL4_GetMR(1);
    ZF_LOGI("Deleting process %d", pid);

    return delete_user_process(pid);
}