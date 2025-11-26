#pragma once
#define N_NAME 32

typedef int pid_t;

typedef struct {
    pid_t     pid;
    unsigned  size;            /* in pages */
    unsigned  stime;           /* start time in msec since booting */
    char      command[N_NAME]; /* Name of executable */
} sos_process_t;