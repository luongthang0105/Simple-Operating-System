#include <sos.h>
#include <assert.h>
#include <tests/macros.h>

#define APP_NAME "console_test"

int test_process_create_and_destroy()
{
    pid_t pid = sos_process_create(APP_NAME);
    assert(pid >= 0);

    int result = sos_process_delete(pid);
    assert(result == 0);
}

int test_single_process_stat()
{
    pid_t pid = sos_process_create(APP_NAME);
    assert(pid >= 0);

    sos_process_t processes[1];
    int result = sos_process_status(&processes, 1);
    assert(result == 1);

    assert(processes[0].command == APP_NAME);
    assert(processes[0].stime > 0);
    assert(processes[0].size > 0);
    assert(processes[0].pid == pid);
}

int test_max_process_stats()
{
    pid_t pid_1 = sos_process_create(APP_NAME);
    assert(pid_1 >= 0);

    pid_t pid_2 = sos_process_create(APP_NAME);
    assert(pid_2 >= 0);

    sos_process_t processes[1];
    int result = sos_process_status(&processes, 1);
    assert(result == 1);

    assert(processes[0].command == APP_NAME);
    assert(processes[0].stime > 0);
    assert(processes[0].size > 0);
    assert(processes[0].pid == pid_1);
}

int test_many_process_stats()
{
    pid_t pid_1 = sos_process_create(APP_NAME);
    assert(pid_1 >= 0);

    pid_t pid_2 = sos_process_create(APP_NAME);
    assert(pid_2 >= 0);

    sos_process_t processes[2];
    int result = sos_process_status(&processes, 2);
    assert(result == 2);

    assert(processes[0].command == APP_NAME);
    assert(processes[0].stime > 0);
    assert(processes[0].size > 0);
    assert(processes[0].pid == pid_1);

    assert(processes[1].command == APP_NAME);
    assert(processes[1].stime > 0);
    assert(processes[1].size > 0);
    assert(processes[1].pid == pid_2);
}

int test_process()
{
    RUN_TEST(test_process_create_and_destroy);
    RUN_TEST(test_single_process_stat);
    RUN_TEST(test_max_process_stats);
    RUN_TEST(test_many_process_stats);
}