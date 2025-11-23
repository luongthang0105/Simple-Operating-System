#include "sys_timestamp.h"

timestamp_t handle_sos_timestamp()
{
    ZF_LOGV("syscall: get timestamp!\n");
    return get_time();
}