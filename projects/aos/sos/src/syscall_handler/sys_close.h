#pragma once
#include <sel4/shared_types_gen.h>
#include <stddef.h>
#include <aos/sel4_zf_logif.h>

typedef struct
{
    size_t thread_index;
    int status;
} nfs_close_cb_args_t;

void handle_sos_close(seL4_MessageInfo_t *reply_msg, int thread_index);