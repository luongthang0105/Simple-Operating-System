#include "sys_stat.h"
#include "../threads.h"
#include <fcntl.h>
#include "../user_process.h"

void sos_stat_callback(int err, struct nfs_context *nfs, void *data, void *private_data)
{
    sos_stat_cb_args_t *ret_private_data = (sos_stat_cb_args_t *)private_data;

    int thread_index = ret_private_data->thread_index;

    if (err < 0)
    {
        ZF_LOGE("error: %d, error msg: %s\n", err, (char *)data);
        ret_private_data->status = -1;
        seL4_Signal(worker_threads[thread_index]->ntfn);
        return;
    }

    struct nfs_stat_64 *nfs_stat = (struct nfs_stat_64 *)data;
    sos_stat_t sos_stat = {
        .st_atime   = nfs_stat->nfs_atime, 
        .st_ctime   = nfs_stat->nfs_ctime,
        .st_size    = nfs_stat->nfs_size,
        .st_type    = ret_private_data->st_type,
        .st_fmode   = 0
    };
    
    if (nfs_stat->nfs_mode & S_IRUSR)
    {
        sos_stat.st_fmode |= FM_READ;
    }
    if (nfs_stat->nfs_mode & S_IWUSR)
    {
        sos_stat.st_fmode |= FM_WRITE;
    }
    if (nfs_stat->nfs_mode & S_IXUSR)
    {
        sos_stat.st_fmode |= FM_EXEC;
    }

    ret_private_data->sos_stat = sos_stat;
    seL4_Signal(worker_threads[thread_index]->ntfn);
}

void handle_sos_stat(seL4_MessageInfo_t *reply_msg, int thread_index)
{
    ZF_LOGV("syscall: stat!\n");
    *reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);

    uintptr_t path_vaddr = seL4_GetMR(1);
    int path_len = seL4_GetMR(2) + 1; // includes terminator
    uintptr_t stat_buf_vaddr = seL4_GetMR(3);

    unsigned char *temp_path_buf = malloc(path_len);
    if (temp_path_buf == NULL)
    {
        ZF_LOGE("Failed to allocate memory for temp_path_buf");
        seL4_SetMR(0, -1);
        return;
    }

    int status = copy_from_user((void *)temp_path_buf, (void *)path_vaddr, path_len);
    if (status == -1)
    {
        free(temp_path_buf);
        seL4_SetMR(0, -1);
        return;
    }

    if (strcmp(temp_path_buf, "..") == 0)
    {
        free(temp_path_buf);
        temp_path_buf = malloc(strlen(".") + 1);
        strcpy(temp_path_buf, ".");
    }

    struct nfs_context *nfs_context = get_nfs_context();
    sos_stat_cb_args_t private_data = {
        .thread_index = thread_index,
        .st_type = strcmp(temp_path_buf, "console") == 0 ? ST_SPECIAL : ST_FILE,
        .status = 0
    };

    int err = nfs_stat64_async(nfs_context, temp_path_buf, sos_stat_callback, &private_data);
    if (err < 0)
    {
        ZF_LOGE("An error occured when trying to queue the command nfs_stat64_async. The callback will not be invoked.");
        free(temp_path_buf);
        seL4_SetMR(0, -1);
        return;
    }

    seL4_Wait(worker_threads[thread_index]->ntfn, NULL);
    if (private_data.status == -1) {
        free(temp_path_buf);
        seL4_SetMR(0, -1);
        return;
    }

    status = copy_to_user((void *)stat_buf_vaddr, (void *)(&private_data.sos_stat), sizeof(sos_stat_t));

    free(temp_path_buf);
    seL4_SetMR(0, status);
}