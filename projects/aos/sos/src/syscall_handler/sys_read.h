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

typedef struct nwcs_input
{
    char arr[DIM];
    size_t i;
    size_t j;
} nwcs_input_t;

extern nwcs_input_t nwcs_input;
extern int nwcs_reader;

void handle_sos_read(seL4_MessageInfo_t *reply_msg, int thread_index);