#include "syscall_handler.h"
#include <sossharedapi/syscalls.h>
#include <utils/util.h>

seL4_MessageInfo_t handle_syscall(UNUSED seL4_Word badge, UNUSED int num_args, bool *have_reply)
{
    seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);

    /* get the first word of the message, which in the SOS protocol is the number
     * of the SOS "syscall". */
    seL4_Word syscall_number = seL4_GetMR(0);

    /* Set the reply flag */
    *have_reply = true;

    int64_t ret = -1;
    printf("receives syscall number = %d\n", syscall_number);
    /* Process system call */
    switch (syscall_number)
    {
    case SYSCALL_NULL_OPS: // intentionally ignores the syscall
        ZF_LOGV("syscall: null ops !\n");
        *have_reply = false;
        ret = 0;
        break;
    case SYSCALL_SOS_OPEN:
        ret = handle_sos_open();
        break;
    case SYSCALL_SOS_CLOSE:
        ret = handle_sos_close(seL4_GetMR(1));
        break;
    case SYSCALL_SOS_WRITE:
        ret = handle_sos_write();
        break;
    case SYSCALL_SOS_READ:
        ret = handle_sos_read();
        break;
    case SYSCALL_SOS_TIMESTAMP:
        ret = handle_sos_timestamp();
        break;
    case SYSCALL_SOS_USLEEP:
        ret = handle_sos_usleep();
        break;
    case SYSCALL_SOS_BRK:
        ret = handle_sos_brk();
        break;
    case SYSCALL_SOS_GETDIRENT:
        ret = handle_sos_getdirent();
        break;
    case SYSCALL_SOS_STAT:
        ret = handle_sos_stat();
        break;
    case SYSCALL_SOS_PROCESS_CREATE:
        ret = handle_sos_process_create();
        break;
    case SYSCALL_SOS_PROCESS_DELETE:
        ret = handle_sos_process_delete();
        break;
    case SYSCALL_SOS_MY_ID:
        ret = handle_sos_my_id();
        break;
    case SYSCALL_SOS_PROCESS_WAIT:
        ret = handle_sos_process_wait();
        break;
    case SYSCALL_SOS_PROCESS_STATUS:
        ret = handle_sos_process_status();
        break;
    default:
        reply_msg = seL4_MessageInfo_new(0, 0, 0, 0);
        ZF_LOGE("Unknown syscall %lu\n", syscall_number);
        /* Don't reply to an unknown syscall */
        *have_reply = false;
    }

    seL4_SetMR(0, ret);
    return reply_msg;
}