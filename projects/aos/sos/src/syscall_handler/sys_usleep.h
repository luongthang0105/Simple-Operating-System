#pragma once
#include <stdint.h>
#include <sel4/shared_types_gen.h>
#include <aos/sel4_zf_logif.h>

void handle_sos_usleep(seL4_MessageInfo_t *reply_msg, int thread_index);