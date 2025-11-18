#pragma once
#include <sel4/shared_types_gen.h>
#include <aos/sel4_zf_logif.h>

typedef struct nfs_opendir_cb_args
{
    int thread_index;
} nfs_opendir_cb_args_t;

void handle_sos_getdirent(seL4_MessageInfo_t *reply_msg, int thread_index);