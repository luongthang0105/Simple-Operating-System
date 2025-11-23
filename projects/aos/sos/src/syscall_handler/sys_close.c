#include "sys_close.h"
#include "../threads.h"
#include "../user_process.h"
#include <nfsc/libnfs.h>
#include <fcntl.h>

void nfs_close_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    if (status < 0)
    {
        ZF_LOGE("nfs_close failed with error: %s\n", (char *)data);
        return;
    }

    nfs_close_cb_args_t *args = private_data;
    size_t thread_index = args->thread_index;
    args->status = status;

    seL4_Signal(worker_threads[thread_index]->ntfn);
    return;
}

int handle_sos_close()
{
    ZF_LOGV("syscall: close!\n");

    user_process_t *user_process = get_current_user_process();

    size_t fd = seL4_GetMR(1);

    if (fd < 0 || fd >= PROCESS_MAX_FILES)
    {
        ZF_LOGE("Invalid file descriptor");
        return -1;
    }

    if (user_process->vfs->fd_table[fd].is_opened == false)
    {
        ZF_LOGE("File is not opened");
        return -1;
    }

    if (fd == CONSOLE_FD)
    {
        user_process->vfs->fd_table[fd].mode = O_WRONLY;
        seL4_SetMR(0, 0);
        return -1;
    }

    struct nfs_context *nfs_context = get_nfs_context();
    nfs_close_cb_args_t args = {.thread_index = current_thread->thread_id};
    int ret = nfs_close_async(nfs_context, user_process->vfs->fd_table[fd].fh, nfs_close_cb, (void *)&args);
    if (ret < 0)
    {
        ZF_LOGE("Failed to queue nfs_close_async");
        return -1;
    }

    seL4_Wait(current_thread->ntfn, NULL);

    if (args.status < 0)
    {
        return -1;
    }

    // Mark fd slot as free. Don't need to free (struct nfsfh*) because it has been freed in nfs_close_async.
    user_process->vfs->fd_table[fd].is_opened = false;
    user_process->vfs->fd_table[fd].mode = -1;
    free(user_process->vfs->fd_table[fd].path);
    
    return 0;
}