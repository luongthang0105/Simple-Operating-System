#include "sys_usleep.h"
#include "../threads.h"
#include <clock/clock.h>

/* Callback for timer registered by usleep system call */
void timeout_callback(uint32_t id, void *data)
{
    int thread_index = *(int *)data;
    seL4_Signal(worker_threads[thread_index]->ntfn);
}

int handle_sos_usleep()
{
    ZF_LOGV("syscall: usleep!\n");
    int msec = seL4_GetMR(1);

    register_timer(msec, timeout_callback, &current_thread->thread_id);
    seL4_Wait(current_thread->ntfn, NULL);
    return 0;
}