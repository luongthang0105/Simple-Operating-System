#include "sys_stat.h"
#include "../threads.h"
#include <fcntl.h>
#include "../user_process.h"
#include "../nfs_wrapper.h"
#include "../network.h"

int handle_sos_stat()
{
    ZF_LOGV("syscall: stat!\n");

    uintptr_t path_vaddr = seL4_GetMR(1);
    int path_len = seL4_GetMR(2) + 1; // includes terminator
    uintptr_t stat_buf_vaddr = seL4_GetMR(3);

    unsigned char *temp_path_buf = malloc(path_len);
    if (temp_path_buf == NULL)
    {
        ZF_LOGE("Failed to allocate memory for temp_path_buf");
        return -1;
    }

    int status = copy_from_user((void *)temp_path_buf, (void *)path_vaddr, path_len);
    if (status == -1)
    {
        free(temp_path_buf);
        return -1;
    }

    if (strcmp((const char*)temp_path_buf, "..") == 0)
    {
        free(temp_path_buf);
        temp_path_buf = malloc(strlen(".") + 1);
        strcpy(temp_path_buf, ".");
    }

    nfs_stat_cb_args_t args = {
        .thread_index = current_thread->thread_id,
        .expected_pid = current_thread->assigned_pid,
        .st_type = strcmp((const char*)temp_path_buf, "console") == 0 ? ST_SPECIAL : ST_FILE,
        .status = 0
    };

    int ret = nfs_stat_wrapper(temp_path_buf, &args);
    if (ret == -1) {
        ZF_LOGE("Failed to get file stat\n");
        return -1;
    }

    status = copy_to_user((void *)stat_buf_vaddr, (void *)(&args.sos_stat), sizeof(sos_stat_t));

    free(temp_path_buf);
    return status;
}