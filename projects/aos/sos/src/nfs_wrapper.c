#include "nfs_wrapper.h"

void nfs_pread_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    nfs_pread_cb_args_t *args = private_data;
    args->bytes_read = status;

    if (status < 0)
    {
        ZF_LOGE("nfs_pread failed with error: %s\n", (char *)data);
        seL4_Signal(worker_threads[args->thread_index]->ntfn);
        return;
    }

    memcpy(args->read_buf, data, status);
    seL4_Signal(worker_threads[args->thread_index]->ntfn);
}

int nfs_pread_wrapper(struct nfsfh* fh, nfs_pread_cb_args_t* args, uint64_t offset, uint64_t count) {
    struct nfs_context *nfs_context = get_nfs_context();

    int ret = nfs_pread_async(nfs_context, fh, offset, count, nfs_pread_cb, (void *)args);
    
    if (ret < 0)
    {
        ZF_LOGE("Failed to queue nfs_read_async");
        return -1;
    }

    seL4_Wait(worker_threads[current_thread->thread_id]->ntfn, NULL);
    
    if (args->bytes_read < 0) {
        ZF_LOGE("Failed to read from NFS");
        return -1;
    }

    return 0;
}

void nfs_close_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    nfs_close_cb_args_t *args = private_data;
    args->status = status;

    if (status < 0)
    {
        ZF_LOGE("nfs_close failed with error: %s\n", (char *)data);
    }

    seL4_Signal(worker_threads[args->thread_index]->ntfn);
    return;
}

int nfs_close_wrapper(struct nfsfh* fh, nfs_close_cb_args_t* args) {
    struct nfs_context *nfs_context = get_nfs_context();

    int ret = nfs_close_async(nfs_context, fh, nfs_close_cb, (void *)args);
    if (ret < 0)
    {
        ZF_LOGE("Failed to queue nfs_close_async");
        return -1;
    }

    seL4_Wait(current_thread->ntfn, NULL);

    if (args->status < 0)
    {
        return -1;
    }
    return 0;
}