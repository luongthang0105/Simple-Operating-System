#include "syscall_handler.h"
#include <sossharedapi/syscalls.h>
#include <utils/util.h>

seL4_MessageInfo_t handle_syscall(UNUSED seL4_Word badge, UNUSED int num_args, bool *have_reply, int thread_index)
{
    seL4_MessageInfo_t reply_msg;

    /* get the first word of the message, which in the SOS protocol is the number
     * of the SOS "syscall". */
    seL4_Word syscall_number = seL4_GetMR(0);

    /* Set the reply flag */
    *have_reply = true;

    /* Process system call */
    /* Ideally, put all of these into syscall_handlers.c (in the future :P)*/
    switch (syscall_number)
    {
    case SYSCALL_SOS_OPEN:
        handle_sos_open(&reply_msg, thread_index);
        break;
    case SYSCALL_SOS_CLOSE:
        handle_sos_close(&reply_msg, thread_index);
        break;
    case SYSCALL_SOS_WRITE:
        handle_sos_write(&reply_msg, thread_index);
        break;
    case SYSCALL_SOS_READ:
        handle_sos_read(&reply_msg, thread_index);
        break;
    case SYSCALL_SOS_TIMESTAMP:
        handle_sos_timestamp(&reply_msg);
        break;
    case SYSCALL_SOS_USLEEP:
        handle_sos_usleep(&reply_msg, thread_index);
        break;
    case SYSCALL_SOS_BRK:
        handle_sos_brk(&reply_msg);
        break;
    case SYSCALL_SOS_GETDIRENT:
        handle_sos_getdirent(&reply_msg, thread_index);
        break;
    case SYSCALL_SOS_STAT:
        handle_sos_stat(&reply_msg, thread_index);
        break;
    default:
        reply_msg = seL4_MessageInfo_new(0, 0, 0, 0);
        ZF_LOGE("Unknown syscall %lu\n", syscall_number);
        /* Don't reply to an unknown syscall */
        *have_reply = false;
    }

    return reply_msg;
}