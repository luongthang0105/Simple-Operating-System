#include "sys_open.h"
#include "../user_process.h"
#include "../pagetable.h"
#include "../threads.h"
#include <fcntl.h>
#include <nfsc/libnfs.h>
#include <sel4/sel4.h>

void sos_open_callback(int err, struct nfs_context *nfs, void *data, void *private_data)
{

    sos_open_cb_args_t *ret_private_data = (sos_open_cb_args_t *)private_data;
    int thread_index = ret_private_data->thread_index;
    int fd = ret_private_data->fd;
    user_process_t *user_process = get_current_user_process_by_thread(thread_index);

    if (err < 0)
    {
        ZF_LOGE("error: %d, error msg: %s\n", err, (char *)data);
        ret_private_data->err = err;
        seL4_Signal(worker_threads[thread_index]->ntfn);
        return;
    }

    struct nfsfh *nfsfh = (struct nfsfh *)data;
    user_process->vfs->fd_table[fd].fh = nfsfh;

    seL4_Signal(worker_threads[thread_index]->ntfn);
}

int handle_sos_open_nwcs(fmode_t mode)
{
    user_process_t *user_process = get_current_user_process();

    sos_fd_t *console = &user_process->vfs->fd_table[CONSOLE_FD];

    bool has_reader = (console->mode == O_RDONLY || console->mode == O_RDWR);
    switch (mode)
    {
    case O_RDONLY:
    case O_RDWR:
        if (has_reader)
            return -1; // only allow one nwcs reader
        console->mode = O_RDWR;
        break;
    case O_WRONLY:
        console->mode = has_reader ? O_RDWR : O_WRONLY;
        break;
    }
    return CONSOLE_FD;
}

int handle_sos_open()
{
    ZF_LOGV("syscall: open!\n");

    user_process_t *user_process = get_current_user_process();

    uintptr_t path_vaddr = seL4_GetMR(1);
    size_t path_len = seL4_GetMR(2);
    fmode_t mode = seL4_GetMR(3);

    unsigned char *path_data = find_frame_data(path_vaddr, user_process->page_global_directory);
    if (!path_data)
    {
        return -1;
    }

    char *temp_path_buf = malloc(path_len + 1);
    if (temp_path_buf == NULL)
    {
        ZF_LOGE("Failed to allocate memory for temp_path_buf");
        return -1;
    }
    temp_path_buf[path_len] = '\0';

    int status = copy_from_user((void *)temp_path_buf, (void *)path_vaddr, path_len);
    if (status == -1)
    {
        free(temp_path_buf);
        return -1;
    }

    if (strcmp(temp_path_buf, "console") == 0)
    {
        free(temp_path_buf);
        return handle_sos_open_nwcs(mode);
    }

    int fd = find_next_fd(user_process->vfs);

    if (fd >= PROCESS_MAX_FILES)
    {
        ZF_LOGE("Unable to allocate a new file descriptor since the number of open files exceeded %d\n", PROCESS_MAX_FILES);
        free(temp_path_buf);
        return -1;
    }

    struct nfs_context *nfs_context = get_nfs_context();
    sos_open_cb_args_t *private_data = malloc(sizeof(sos_open_cb_args_t));
    if (private_data == NULL)
    {
        ZF_LOGE("Failed to allocate memory for nfs_open callback private_data");
        free(temp_path_buf);
        return -1;
    }

    private_data->thread_index = current_thread->thread_id;
    private_data->fd = fd;
    private_data->err = 0;

    int err = nfs_open_async(nfs_context, temp_path_buf, mode | O_CREAT, sos_open_callback, private_data);

    if (err)
    {
        ZF_LOGE("An error occured when trying to queue the command nfs_open_async. The callback will not be invoked.");
        free(temp_path_buf);
        free(private_data);
        return -1;
    }

    seL4_Wait(current_thread->ntfn, NULL);

    err = private_data->err;
    free(private_data);

    if (err)
    {
        free(temp_path_buf);
        return -1;
    }

    user_process->vfs->fd_table[fd].is_opened = true;
    user_process->vfs->fd_table[fd].mode = mode;
    user_process->vfs->fd_table[fd].path = temp_path_buf;

    return fd;
}