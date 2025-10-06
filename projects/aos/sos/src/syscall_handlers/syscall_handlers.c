#include "syscall_handlers.h"
#include <sel4/functions.h>
#include <aos/sel4_zf_logif.h>

void handler_sos_write(void *args)
{

    // ZF_LOGV("syscall write!\n");
    // char byte_to_send[1] = { seL4_GetMR(1) };
    // network_console_send(nwcs, byte_to_send, 1);
}
