#pragma once
#include <sel4/shared_types_gen.h>
#include <stddef.h>
#include <aos/sel4_zf_logif.h>

typedef struct
{
    size_t thread_index;
    int status;
} nfs_close_cb_args_t;

/**
 * @brief Handles the sos_close syscall for a user process.
 *
 * Validates the provided file descriptor, handles the console special case,
 * and issues an asynchronous NFS close request for regular files. The calling
 * thread blocks until the close operation completes. On success, the file
 * descriptor entry is cleared from the process’s VFS table.
 *
 * @return 0 on success, or -1 on failure.
 */
int handle_sos_close();