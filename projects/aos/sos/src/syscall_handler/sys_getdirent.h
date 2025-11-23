#pragma once
#include <sel4/shared_types_gen.h>
#include <aos/sel4_zf_logif.h>

typedef struct nfs_opendir_cb_args
{
    int thread_index;
} nfs_opendir_cb_args_t;

/**
 * @brief Handles the sos_getdirent syscall for a user process.
 *
 * Retrieves the directory entry at a given position in the current directory.
 * Opens the directory via asynchronous NFS operations, reads entries until
 * the requested position, and copies the entry name into user memory. Returns
 * the number of bytes copied on success, 0 if the position is past the last
 * entry, or -1 on failure.
 *
 * @return Number of bytes copied on success, 0 if no entry exists at `pos`,
 *         or -1 on error.
 */
int handle_sos_getdirent();