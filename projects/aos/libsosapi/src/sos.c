/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */
#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sos.h>
#include <stdbool.h>
#include <sel4/sel4.h>
#include <fcntl.h>
#include <nfsc/libnfs.h>

static size_t sos_debug_print(const void *vData, size_t count)
{
#ifdef CONFIG_DEBUG_BUILD
    size_t i;
    const char *realdata = vData;
    for (i = 0; i < count; i++) {
        seL4_DebugPutChar(realdata[i]);
    }
#endif
    return count;
}

// currently does not check for PROCESS_MAX_FILES opened
int sos_open(const char *path, int flag)
{
    fmode_t mode;
    switch (flag)
    {
        case O_RDONLY:
            mode = FM_READ;
            break;
        case O_WRONLY:
            mode = FM_WRITE;
            break;
        case O_RDWR:
            mode = FM_WRITE | FM_READ;
            break;
        default:
            return -1;
            break;
    }   

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 5);
    seL4_SetMR(0, SYSCALL_SOS_OPEN);
    seL4_SetMR(1, path);
    seL4_SetMR(2, strlen(path));
    seL4_SetMR(3, flag);
    seL4_SetMR(4, mode);

    seL4_Call(SOS_IPC_EP_CAP, tag);
   return seL4_GetMR(0);
}

int sos_close(int file)
{
    if (file < 0) return -1;
    if (file == CONSOLE_FD) {
        return 0;
    }
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 5);
    seL4_SetMR(0, SYSCALL_SOS_CLOSE);
    seL4_SetMR(1, file);

    seL4_Call(SOS_IPC_EP_CAP, tag);
   return seL4_GetMR(0);
}

int sos_read(int file, char *buf, size_t nbyte)
{   
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 4);
    seL4_SetMR(0, SYSCALL_SOS_READ); 
    seL4_SetMR(1, buf);
    seL4_SetMR(2, nbyte);
    seL4_SetMR(3, file);

    seL4_Call(SOS_IPC_EP_CAP, tag);
    
    seL4_Word num_byte_read = seL4_GetMR(0);
    return num_byte_read;
}

int sos_write(int file, const char *buf, size_t nbyte)
{
    if (file == 1 || file == 2) { /* stdout/stderr, let it writes to network console */
        file = CONSOLE_FD;   
    }

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 4);
    seL4_SetMR(0, SYSCALL_SOS_WRITE); 
    seL4_SetMR(1, buf);
    seL4_SetMR(2, nbyte);
    seL4_SetMR(3, file);
    seL4_Call(SOS_IPC_EP_CAP, tag);

    return seL4_GetMR(0);
}

int sos_getdirent(int pos, char *name, size_t nbyte)
{
    if (pos < 0) {
        return -1;
    }

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 4);
    seL4_SetMR(0, SYSCALL_SOS_GETDIRENT); 
    seL4_SetMR(1, pos);
    seL4_SetMR(2, name);
    seL4_SetMR(3, nbyte);
    seL4_Call(SOS_IPC_EP_CAP, tag);

    return seL4_GetMR(0);
}

int sos_stat(const char *path, sos_stat_t *buf)
{
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 4);
    seL4_SetMR(0, SYSCALL_SOS_STAT);
    seL4_SetMR(1, path);
    seL4_SetMR(2, strlen(path));
    seL4_SetMR(3, buf);
    seL4_Call(SOS_IPC_EP_CAP, tag);

    return seL4_GetMR(0);
}

pid_t sos_process_create(const char *path)
{
    assert(!"You need to implement this");
    return -1;
}

int sos_process_delete(pid_t pid)
{
    // assert(!"You need to implement this");
    return -1;
}

pid_t sos_my_id(void)
{

    // assert(!"You need to implement this");
    return -1;

}

int sos_process_status(sos_process_t *processes, unsigned max)
{
    assert(!"You need to implement this");
    return -1;
}

pid_t sos_process_wait(pid_t pid)
{
    assert(!"You need to implement this");
    return -1;

}

void sos_usleep(int usec)
{
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(0, SYSCALL_SOS_USLEEP);
    seL4_SetMR(1, usec);
    seL4_Call(SOS_IPC_EP_CAP, tag);
}

int64_t sos_time_stamp(void)
{
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, SYSCALL_SOS_TIMESTAMP); 
    seL4_Call(SOS_IPC_EP_CAP, tag);
    int64_t timestamp = seL4_GetMR(0);
    return timestamp;
}
