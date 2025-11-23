#include "sys_getdirent.h"
#include "../user_process.h"
#include "../threads.h"

extern user_process_t user_process;
extern sos_thread_t *worker_threads[MAX_WORKER_THREADS];

void nfs_opendir_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    int thread_index = ((nfs_opendir_cb_args_t *)private_data)->thread_index;

    user_process_t *user_process = get_current_user_process_by_thread(thread_index);

    if (status < 0)
    {
        user_process->curr_dir = NULL;
        ZF_LOGE("nfs_opendir failed with error: %s\n", (char *)data);
        return;
    }

    user_process->curr_dir = (struct nfsdir *)data;
    seL4_Signal(worker_threads[thread_index]->ntfn);

    return;
}

int handle_sos_getdirent()
{
    ZF_LOGV("syscall: getdirent!\n");

    user_process_t *user_process = get_current_user_process();

    size_t pos = seL4_GetMR(1);
    uintptr_t buf_vaddr = seL4_GetMR(2);
    size_t nbyte = seL4_GetMR(3);

    struct nfs_context *nfs_context = get_nfs_context();

    // calls opendir to get struct nfsdir*
    nfs_opendir_cb_args_t args = {.thread_index = current_thread->thread_id};
    int ret = nfs_opendir_async(nfs_context, "./", nfs_opendir_cb, (void *)&args);
    if (ret < 0)
    {
        ZF_LOGE("Failed to queue nfs_opendir_async");
        return -1;
    }

    seL4_Wait(current_thread->ntfn, NULL);

    // calls nfs_readdir to read the expected entry (struct dirent*)
    struct nfsdirent *nfsdirent;
    for (size_t i = 0; i <= pos; ++i)
    {
        nfsdirent = nfs_readdir(nfs_context, user_process->curr_dir);
        if (nfsdirent == NULL)
        {
            if (i == pos)
            { // pos is right after the last entry
                return 0;
            }
            else
            { // otherwise, treat this as non-existent entry
                return -1;
            }
        }
    }
    // gets the name field, and copy it to the name buf (including null terminator)
    size_t bytes_to_copy = MIN(nbyte, strlen(nfsdirent->name) + 1);
    int status = copy_to_user((void *)buf_vaddr, (void *)nfsdirent->name, bytes_to_copy);

    if (status == 0)
    {
        return bytes_to_copy;
    }
    else
    {
        return -1;
    }
}
