#pragma once
#include <stddef.h>
#include <sel4/shared_types_gen.h>
#include <aos/sel4_zf_logif.h>

typedef struct
{
    size_t thread_index;
    size_t bytes_written;
} nfs_write_cb_args_t;

#define BREAKDOWN_THRESHOLD BIT(16)

/**
 * @brief Handles the sos_write syscall for a user process.
 *
 * Validates the file descriptor and write permissions, copies data from
 * user memory into a temporary kernel buffer, and writes the data either to
 * the console or to an NFS-backed file in chunks. For NFS files, the write
 * is performed via asynchronous NFS operations, and the calling thread blocks
 * until each chunk completes. Returns the total number of bytes successfully
 * written.
 *
 * @return The number of bytes written on success, or -1 on failure.
 */
int handle_sos_write();
