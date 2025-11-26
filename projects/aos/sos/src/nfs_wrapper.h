#pragma once
#include <aos/sel4_zf_logif.h>
#include "threads.h"

typedef struct nfs_pread_cb_args {
    uint32_t thread_index;
    int bytes_read;
    unsigned char *read_buf;
} nfs_pread_cb_args_t;

void nfs_pread_cb(int status, struct nfs_context *nfs, void *data, void *private_data);

/**
 * A wrapper function to call nfs_pread_async. The function will block the current thread, waiting for `nfs_pread_cb` to finish execution.
 * 
 * @returns 0 on success, -1 otherwise.
 */
int nfs_pread_wrapper(struct nfsfh* fh, nfs_pread_cb_args_t* args, uint64_t offset, uint64_t count);

typedef struct
{
    size_t thread_index;
    int status;
} nfs_close_cb_args_t;
void nfs_close_cb(int status, struct nfs_context *nfs, void *data, void *private_data);
int nfs_close_wrapper(struct nfsfh* fh, nfs_close_cb_args_t* args);