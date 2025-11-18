#include "sys_timestamp.h"
#include <clock/clock.h>

void handle_sos_timestamp(seL4_MessageInfo_t *reply_msg)
{
    ZF_LOGV("syscall: get timestamp!\n");
    *reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, get_time());
}