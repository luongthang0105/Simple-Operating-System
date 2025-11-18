#pragma once
#include <stdint.h>
#include <sossharedapi/vfs.h>
#include <sel4/shared_types_gen.h>
#include <aos/sel4_zf_logif.h>

typedef struct sos_stat_cb_args {
    int thread_index;
    uintptr_t stat_buf_vaddr;
    st_type_t st_type;
    int status;
} sos_stat_cb_args_t;

void sos_stat_callback(int err, struct nfs_context *nfs, void *data, void *private_data);
