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

static bool is_console_opened = false;
static fmode_t console_mode = 0;

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
int sos_open(const char *path, fmode_t mode)
{
    switch (mode)
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

    if (strcmp(path, "console") == 0) {
        if (is_console_opened) {
            if (HAS_FM_READ(console_mode) && HAS_FM_READ(mode)) {
                return -1; // Only one reader at a time!
            }
        } 

        is_console_opened = true;
        console_mode |= mode;

        return CONSOLE_FD;
    }

    return -1;
}

int sos_close(int file)
{
    assert(!"You need to implement this");
    return -1;
}

int sos_read(int file, char *buf, size_t nbyte)
{
    // check invalid file
    if (!is_console_opened || !HAS_FM_READ(console_mode)) {
        return -1;
    }
    int num_byte_read = 0;
    for (size_t i = 0; i < nbyte; ++i) {
        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
        seL4_SetMR(0, SYSCALL_SOS_READ); 
        seL4_SetMR(1, file);
        
        seL4_Call(SOS_IPC_EP_CAP, tag);
        seL4_Word character = seL4_GetMR(0);
        buf[i] = character;
        num_byte_read += 1;
        if (character == '\n') {
            break;
        }
    }
    return num_byte_read;
}

int sos_write(int file, const char *buf, size_t nbyte)
{
    if (!is_console_opened || !HAS_FM_WRITE(console_mode)) return -1;

    for (size_t i = 0; i < nbyte; ++i) {
        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 3);
        seL4_SetMR(0, SYSCALL_SOS_WRITE); 
        seL4_SetMR(1, buf[i]);
        seL4_SetMR(2, file);
        seL4_Call(SOS_IPC_EP_CAP, tag);
    }
    return nbyte;

}

int sos_getdirent(int pos, char *name, size_t nbyte)
{
    assert(!"You need to implement this");
    return -1;
}

int sos_stat(const char *path, sos_stat_t *buf)
{
    assert(!"You need to implement this");
    return -1;
}

pid_t sos_process_create(const char *path)
{
    assert(!"You need to implement this");
    return -1;
}

int sos_process_delete(pid_t pid)
{
    assert(!"You need to implement this");
    return -1;
}

pid_t sos_my_id(void)
{
    assert(!"You need to implement this");
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

void sos_usleep(int msec)
{
    assert(!"You need to implement this");
}

int64_t sos_time_stamp(void)
{
    assert(!"You need to implement this");
    return -1;
}
