#pragma once
#include <stdint.h>
#include <sossharedapi/vfs.h>
#include <sel4/shared_types_gen.h>
#include <aos/sel4_zf_logif.h>

typedef struct sos_stat_cb_args {
    int thread_index;
    sos_stat_t sos_stat;
    st_type_t st_type;
    int status;
} sos_stat_cb_args_t;

void handle_sos_stat(seL4_MessageInfo_t *reply_msg, int thread_index);

