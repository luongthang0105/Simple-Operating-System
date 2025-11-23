#pragma once
#include <stdint.h>
#include <sossharedapi/vfs.h>
#include <sel4/shared_types_gen.h>
#include <aos/sel4_zf_logif.h>

typedef struct sos_stat_cb_args
{
    int thread_index;
    sos_stat_t sos_stat;
    st_type_t st_type;
    int status;
} sos_stat_cb_args_t;

/**
 * @brief Handles the sos_stat syscall for a user process.
 *
 * Copies the file path from user memory, performs an asynchronous NFS stat
 * operation (or handles special paths like "console"), waits for completion,
 * and writes the resulting file status structure back to user memory.
 *
 * @return 0 on success, or -1 on failure.
 */
int handle_sos_stat();
