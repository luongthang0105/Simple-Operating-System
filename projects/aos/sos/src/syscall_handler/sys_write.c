#include "sys_write.h"
#include "../user_process.h"
#include "../threads.h"
#include <fcntl.h>
#include "networkconsole/networkconsole.h"

void nfs_write_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    nfs_write_cb_args_t *args = private_data;
    size_t thread_index = args->thread_index;
    args->bytes_written = status;

    if (status < 0)
    {
        ZF_LOGE("nfs_write failed with error: %s\n", (char *)data);
        seL4_Signal(worker_threads[thread_index]->ntfn);
        return;
    }
    seL4_Signal(worker_threads[thread_index]->ntfn);
    return;
}

void handle_sos_write(seL4_MessageInfo_t *reply_msg, size_t thread_index)
{
    ZF_LOGV("syscall: write!\n");

    *reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);

    uintptr_t buf_vaddr = seL4_GetMR(1);
    size_t nbytes = seL4_GetMR(2);
    int file_desc = seL4_GetMR(3);

    if (file_desc < 0 || file_desc >= PROCESS_MAX_FILES)
    {
        ZF_LOGE("File descriptor is invalid.");
        seL4_SetMR(0, -1);
        return;
    }

    if (!user_process.vfs->fd_table[file_desc].is_opened)
    {
        ZF_LOGE("File %zu is not open yet!", file_desc);
        seL4_SetMR(0, -1);
        return;
    }

    if (user_process.vfs->fd_table[file_desc].mode != O_WRONLY &&
        user_process.vfs->fd_table[file_desc].mode != O_RDWR)
    {
        ZF_LOGE("File %zu is not open to write!", file_desc);
        seL4_SetMR(0, -1);
        return;
    }

    if (file_desc != CONSOLE_FD)
    { /* normal files */
        if (user_process.vfs->fd_table[file_desc].fh == NULL)
        {
            ZF_LOGE("NFS file handle for fd=%d does not exist", file_desc);
            seL4_SetMR(0, -1);
            return;
        }
    }

    size_t total_bytes_written = 0;

    /* only need to allocate once with the biggest size it needs */
    unsigned char *temp_buf = malloc(MIN(BREAKDOWN_THRESHOLD, nbytes));
    if (temp_buf == NULL)
    {
        ZF_LOGE("Failed to allocate memory for temp_buf");
        seL4_SetMR(0, -1);
        return;
    }

    while (nbytes > 0)
    {
        size_t bytes_to_write = MIN(BREAKDOWN_THRESHOLD, nbytes);
        size_t bytes_written = 0;

        int status = copy_from_user(temp_buf, (void *)(buf_vaddr + total_bytes_written), bytes_to_write);
        if (status == -1)
        {
            free(temp_buf);
            seL4_SetMR(0, -1);
            return;
        }

        if (file_desc != CONSOLE_FD)
        { /* normal files */
            struct nfs_context *nfs_context = get_nfs_context();

            nfs_write_cb_args_t args = {.thread_index = thread_index};
            int ret = nfs_write_async(nfs_context, user_process.vfs->fd_table[file_desc].fh, bytes_to_write, (const void *)temp_buf,
                                      nfs_write_cb, (void *)&args);
            if (ret < 0)
            {
                ZF_LOGE("Failed to queue nfs_write_async");
                seL4_SetMR(0, -1);
                free(temp_buf);
                return;
            }
            seL4_Wait(worker_threads[thread_index]->ntfn, NULL);

            bytes_written = args.bytes_written;
        }
        else
        { /* console file, send it to network console */
            int bytes_sent = network_console_send(get_nwcs(), temp_buf, bytes_to_write);
            if (bytes_sent == -1)
            {
                ZF_LOGE("Failed to send %lu bytes via network_console_send", bytes_to_write);
                free(temp_buf);
                seL4_SetMR(0, -1);
                return;
            }

            bytes_written += bytes_sent;
        }

        total_bytes_written += bytes_written;
        nbytes -= bytes_written;
    }

    free(temp_buf);
    seL4_SetMR(0, total_bytes_written);
    return;
}
