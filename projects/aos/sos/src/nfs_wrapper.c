#include "nfs_wrapper.h"
#include <fcntl.h>
#include "network.h"

void nfs_stat_cb(int err, UNUSED struct nfs_context *nfs, void *data, void *private_data)
{
    sync_recursive_mutex_lock(worker_threads_mutex);
    nfs_stat_cb_args_t *args = (nfs_stat_cb_args_t *)private_data;

    int thread_index = args->thread_index;
    pid_t expected_pid = args->expected_pid;

    sos_thread_t *worker_thread = worker_threads[thread_index];
    if (expected_pid != worker_thread->assigned_pid) {
        sync_recursive_mutex_unlock(worker_threads_mutex);
        return;
    }

    if (err < 0)
    {
        ZF_LOGE("error: %d, error msg: %s\n", err, (char *)data);
        args->status = -1;
        seL4_Signal(worker_thread->ntfn);
        sync_recursive_mutex_unlock(worker_threads_mutex);
        return;
    }

    struct nfs_stat_64 *nfs_stat = (struct nfs_stat_64 *)data;
    sos_stat_t sos_stat = {
        .st_atime   = nfs_stat->nfs_atime, 
        .st_ctime   = nfs_stat->nfs_ctime,
        .st_size    = nfs_stat->nfs_size,
        .st_type    = ST_FILE,
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

    args->sos_stat = sos_stat;
    seL4_Signal(worker_thread->ntfn);
    sync_recursive_mutex_unlock(worker_threads_mutex);

}

int nfs_stat_wrapper(unsigned char *temp_path_buf, nfs_stat_cb_args_t* args) {
    struct nfs_context *nfs_context = get_nfs_context();

    int err = nfs_stat64_async(nfs_context, (const char*)temp_path_buf, nfs_stat_cb, (void *)args);
    if (err < 0)
    {
        ZF_LOGE("An error occured when trying to queue the command nfs_stat64_async. The callback will not be invoked.");
        free(temp_path_buf);
        return -1;
    }

    seL4_Wait(current_thread->ntfn, NULL);
    if (args->status < 0) {
        free(temp_path_buf);
        return -1;
    }

    return 0;
}
void nfs_pread_cb(int status, UNUSED struct nfs_context *nfs, void *data, void *private_data)
{
    sync_recursive_mutex_lock(worker_threads_mutex);

    nfs_pread_cb_args_t *args = private_data;
    args->bytes_read = status;

    int thread_index = args->thread_index;
    pid_t expected_pid = args->expected_pid;

    sos_thread_t *worker_thread = worker_threads[thread_index];
    if (expected_pid != worker_thread->assigned_pid) {
        sync_recursive_mutex_unlock(worker_threads_mutex);
        return;
    }

    if (status < 0)
    {
        ZF_LOGE("nfs_pread failed with error: %s\n", (char *)data);
        seL4_Signal(worker_threads[args->thread_index]->ntfn);
        sync_recursive_mutex_unlock(worker_threads_mutex);
        return;
    }

    memcpy(args->read_buf, data, status);
    seL4_Signal(worker_threads[args->thread_index]->ntfn);

    sync_recursive_mutex_unlock(worker_threads_mutex);
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
    sync_recursive_mutex_lock(worker_threads_mutex);

    nfs_close_cb_args_t *args = private_data;
    args->status = status;

    int thread_index = args->thread_index;
    pid_t expected_pid = args->expected_pid;

    sos_thread_t *worker_thread = worker_threads[thread_index];
    if (expected_pid != worker_thread->assigned_pid) {
        sync_recursive_mutex_unlock(worker_threads_mutex);
        return;
    }
    
    if (status < 0)
    {
        ZF_LOGE("nfs_close failed with error: %s\n", (char *)data);
    }

    seL4_Signal(worker_threads[args->thread_index]->ntfn);
    sync_recursive_mutex_unlock(worker_threads_mutex);

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