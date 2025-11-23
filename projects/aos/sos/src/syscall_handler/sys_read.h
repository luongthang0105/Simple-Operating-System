#pragma once
#include <utils/sglib.h>
#include <sel4/shared_types_gen.h>
#include <stddef.h>
#include <aos/sel4_zf_logif.h>

typedef struct
{
    size_t thread_index;
    size_t bytes_read;
    unsigned char *data;
} nfs_read_cb_args_t;

#define DIM (size_t)8092
/**
 * Buffer for storing incoming console input (NWCS) before it is read
 * by user processes. Acts as a temporary queue of characters until a
 * reading thread consumes them.
 */
typedef struct nwcs_input
{
    char arr[DIM];
    size_t i;
    size_t j;
} nwcs_input_t;
extern nwcs_input_t nwcs_input;

/**
 * ID of the thread currently blocked waiting for console input,
 * or -1 when no thread is waiting.
 */
extern int nwcs_reader;

/**
 * @brief Handles the sos_read syscall for a user process.
 *
 * Validates the file descriptor and read permissions, then reads data either
 * from an NFS-backed file or from the console input buffer. Data is copied
 * into a temporary kernel buffer and then back into user memory in chunks.
 * Console reads block until input becomes available and may return early on
 * newline. NFS reads use asynchronous operations, and the calling thread
 * blocks until completion. Returns the total number of bytes successfully read.
 *
 * @return The number of bytes read on success, or -1 on failure.
 */
int handle_sos_read();