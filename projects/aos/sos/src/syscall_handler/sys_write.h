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

void handle_sos_write(seL4_MessageInfo_t *reply_msg, size_t thread_index);
