#pragma once

/** This is used for situation where a syscall is received, but it should be ignored.
 *  For example, process A's worker thread enters process_wait(), waiting for process B.
 * 
 *  Process C then kills process A, then restarts process A's worker thread to syscall_loop().
 *
 *  Process C kills process B, which will trigger signal_then_destroy_caps(), hence signals process A's worker thread.
 *  
 *  Process A's worker thread receives the null ops syscall, 
 *  which should be ignored because it is no longer waiting for process B to finish.
 *
*/
#define SYSCALL_NULL_OPS            0

/* Syscall numbers */
enum syscall_numbers {
    SYSCALL_SOS_OPEN = 1,
    SYSCALL_SOS_CLOSE,
    SYSCALL_SOS_READ,
    SYSCALL_SOS_WRITE,
    SYSCALL_SOS_TIMESTAMP,
    SYSCALL_SOS_USLEEP,
    SYSCALL_SOS_BRK,
    SYSCALL_SOS_GETDIRENT,
    SYSCALL_SOS_STAT,
    SYSCALL_SOS_PROCESS_CREATE,
    SYSCALL_SOS_PROCESS_DELETE,
    SYSCALL_SOS_MY_ID,
    SYSCALL_SOS_PROCESS_WAIT,
    SYSCALL_SOS_PROCESS_STATUS
};