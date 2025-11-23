#pragma once
#include <stddef.h>
#include <sel4/shared_types_gen.h>
#include <aos/sel4_zf_logif.h>

typedef struct sos_open_cb_args
{
    int thread_index;
    int fd;
    int err;
} sos_open_cb_args_t;

/**
 * @brief Handles the sos_open syscall for a user process.
 *
 * Retrieves the file path and mode from user memory, validates and copies the
 * path into kernel space, and then performs an asynchronous NFS open request.
 * The calling thread blocks until the NFS operation completes. On success, a
 * new file descriptor is allocated and recorded in the process’s VFS table.
 *
 * @return A file descriptor on success, or -1 on failure.
 */
int handle_sos_open();
