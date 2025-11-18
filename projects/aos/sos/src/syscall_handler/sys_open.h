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

void handle_sos_open(seL4_MessageInfo_t *reply_msg, int thread_index);
