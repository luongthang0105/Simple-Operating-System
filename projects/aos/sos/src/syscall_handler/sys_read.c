#include "sys_read.h"
#include "sys_write.h"
#include "../user_process.h"
#include "../threads.h"
#include <fcntl.h>

SGLIB_DEFINE_QUEUE_FUNCTIONS(nwcs_input_t, char, arr, i, j, DIM);
nwcs_input_t nwcs_input = {.arr = {0}, .i = 0, .j = 0};
int nwcs_reader = -1; // thread index that is currently the nwcs reader

void nfs_read_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    nfs_read_cb_args_t *args = private_data;
    args->bytes_read = status;

    if (status < 0)
    {
        ZF_LOGE("nfs_read failed with error: %s\n", (char *)data);
        seL4_Signal(worker_threads[args->thread_index]->ntfn);
        return;
    }

    memcpy(args->data, data, status);
    seL4_Signal(worker_threads[args->thread_index]->ntfn);
    return;
}
/*  nwcs_reader must be set to -1 before the function retunrs.
    write_to_buf() will signal when nwcs_reader != -1, so not setting it back to -1
    will make write_to_buf() signals the syscall loop instead.
*/
void handle_sos_read(seL4_MessageInfo_t *reply_msg, int thread_index)
{
    ZF_LOGV("syscall: read!\n");
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
        ZF_LOGE("File is not open yet!");
        seL4_SetMR(0, -1);
        return;
    }

    if (user_process.vfs->fd_table[file_desc].mode != O_RDONLY &&
        user_process.vfs->fd_table[file_desc].mode != O_RDWR)
    {
        ZF_LOGE("File %d is not open to read!", file_desc);
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

    size_t total_bytes_read = 0;

    /* only need to allocate once with the biggest size it needs */
    unsigned char *data = malloc(MIN(BREAKDOWN_THRESHOLD, nbytes));
    if (data == NULL)
    {
        ZF_LOGE("Failed to allocate memory");
        seL4_SetMR(0, -1);
        return;
    }

    while (nbytes > 0)
    {
        size_t bytes_to_read = MIN(BREAKDOWN_THRESHOLD, nbytes);
        size_t bytes_read = 0;
        bool early_return = false;
        bool failed = false;

        if (file_desc != CONSOLE_FD)
        { /* normal files */
            struct nfs_context *nfs_context = get_nfs_context();

            nfs_read_cb_args_t args = {.thread_index = thread_index, .data = data};
            int ret = nfs_read_async(nfs_context, user_process.vfs->fd_table[file_desc].fh, bytes_to_read,
                                     nfs_read_cb, (void *)&args);
            if (ret < 0)
            {
                ZF_LOGE("Failed to queue nfs_read_async");
                free(data);
                seL4_SetMR(0, -1);
                return;
            }
            seL4_Wait(worker_threads[thread_index]->ntfn, NULL);
            bytes_read = args.bytes_read;

            if (bytes_read == 0)
            { // assuming this is EOF
                early_return = true;
            }
            else if (bytes_read == -1)
            {
                failed = true;
            }
        }
        else
        {
            size_t remaining_bytes = bytes_to_read;
            while (remaining_bytes > 0)
            {
                if (sglib_nwcs_input_t_is_empty(&nwcs_input))
                {
                    nwcs_reader = thread_index;
                    seL4_Wait(worker_threads[thread_index]->ntfn, NULL);
                }

                data[bytes_read] = sglib_nwcs_input_t_first_element(&nwcs_input);
                sglib_nwcs_input_t_delete_first(&nwcs_input);
                if (data[bytes_read] == '\n')
                {
                    bytes_read++;
                    early_return = true;
                    break;
                }

                remaining_bytes--;
                bytes_read++;
            }
            nwcs_reader = -1;
        }

        int status = copy_to_user((void *)(buf_vaddr + total_bytes_read), (void *)data, bytes_read);
        if (status == -1)
        {
            failed = true;
        }

        total_bytes_read += bytes_read;
        nbytes -= bytes_read;

        if (failed)
        { /* must check for failed before early_return because failing cases is more important (higher priority) then */
            free(data);
            seL4_SetMR(0, -1);
            return;
        }

        if (early_return)
        {
            free(data);
            seL4_SetMR(0, total_bytes_read);
            return;
        }
    }

    free(data);
    seL4_SetMR(0, total_bytes_read);
    return;
}
