#include "sys_usleep.h"
#include "../threads.h"
#include <clock/clock.h>

/* Callback for timer registered by usleep system call */
void timeout_callback(uint32_t id, void *data)
{
    int thread_index = *(int *)data;
    seL4_Signal(worker_threads[thread_index]->ntfn);
}

void handle_sos_usleep(seL4_MessageInfo_t *reply_msg, int thread_index)
{
    ZF_LOGV("syscall: usleep!\n");
    int msec = seL4_GetMR(1);

    *reply_msg = seL4_MessageInfo_new(0, 0, 0, 0);

    register_timer(msec, timeout_callback, &thread_index);
    seL4_Wait(worker_threads[thread_index]->ntfn, NULL);
}