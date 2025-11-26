#include "../threads.h"
#include "../user_process.h"
#include <nfsc/libnfs.h>
#include <fcntl.h>
#include "sys_read.h"
#include "../nfs_wrapper.h"

int handle_sos_close(size_t fd)
{
    ZF_LOGV("syscall: close!\n");

    user_process_t *user_process = get_current_user_process();

    sos_fd_t *file = &user_process->vfs->fd_table[fd];

    if (fd < 0 || fd >= PROCESS_MAX_FILES)
    {
        ZF_LOGE("Invalid file descriptor");
        return -1;
    }

    if (file->is_opened == false)
    {
        ZF_LOGE("File is not opened");
        return -1;
    }

    if (fd == STDOUT_FD || fd == STDERR_FD) return 0;

    if (fd == STDIN_FD) {
        file->is_opened = false;
        file->mode = -1;
        update_nwcs_reader(-1);
        return 0;
    }
    
    if (fd == CONSOLE_FD)
    {   
        bool is_reader = (file->mode == O_RDONLY) || (file->mode == O_RDWR);
        if (is_reader) {
            update_nwcs_reader(-1);
            file->mode = O_WRONLY;
        }
        return 0;
    }

    struct nfs_context *nfs_context = get_nfs_context();
    nfs_close_cb_args_t args = {.thread_index = current_thread->thread_id};
    int ret = nfs_close_wrapper(file->fh, &args);
    if (ret == -1) {
        ZF_LOGE("Failed to close file, fd = %d\n", fd);
        return -1;
    }
    // Mark fd slot as free. Don't need to free (struct nfsfh*) because it has been freed in nfs_close_async.
    file->is_opened = false;
    file->mode = -1;
    free(file->path);
    
    return 0;
}