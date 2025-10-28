#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <utils/time.h>
#include <syscalls.h>
/* Your OS header file */
#include <sos.h>

#define BUF_SIZE    6144
#define MAX_ARGS   32
static int fd;
static sos_stat_t sbuf;

//========================== OPEN ==========================
int test_open_console() {
    fd = open("console", O_RDONLY);
    assert(fd == CONSOLE_FD);

    close(CONSOLE_FD); // clean up the state for the next test
}

int test_open_console_with_two_readers() {
    fd = open("console", O_RDONLY);
    assert(fd == CONSOLE_FD);

    fd = open("console", O_RDONLY);
    assert(fd == -1);

    fd = open("console", O_RDWR);
    assert(fd == -1);
    
    close(CONSOLE_FD); // clean up the state for the next test
}

int test_open_console_with_multiple_writers() {
    fd = open("console", O_WRONLY);
    assert(fd == CONSOLE_FD);

    fd = open("console", O_WRONLY);
    assert(fd == CONSOLE_FD);

    fd = open("console", O_WRONLY);
    assert(fd == CONSOLE_FD);

    fd = open("console", O_RDWR);
    assert(fd == CONSOLE_FD);

    close(CONSOLE_FD); // clean up the state for the next test
}

int test_open_console_with_read_and_write() {
    fd = open("console", O_RDONLY);
    assert(fd == CONSOLE_FD);

    fd = open("console", O_WRONLY);
    assert(fd == CONSOLE_FD);

    close(CONSOLE_FD); // clean up the state for the next test
}

int test_open_non_existent_file() {
    fd = open("new_file.txt", O_RDONLY);
    assert(fd > 0);

    int res = sos_stat("new_file.txt", &sbuf);
    assert(res == 0);

    assert(sbuf.st_type == ST_FILE);

    // Non-existent file should have read-write permission
    assert((sbuf.st_fmode & FM_READ) != 0);
    assert((sbuf.st_fmode & FM_WRITE) != 0);
    assert((sbuf.st_fmode & FM_EXEC) == 0);
    
    close(fd); // clean up the state for the next test
}

//========================== READ ==========================
int test_read_file_opened_with_read_mode() {
    fd = open("test_file_read_empty.txt", O_RDONLY);
    char buf[BUF_SIZE];
    assert(read(fd, buf, BUF_SIZE) == 0);

    close(fd); // clean up the state for the next test
}

int test_read_file_opened_with_readwrite_mode() {
    fd = open("test_file_read_empty.txt", O_RDWR);
    char buf[BUF_SIZE];
    assert(read(fd, buf, BUF_SIZE) == 0);

    close(fd); // clean up the state for the next test
}

int test_read_file_opened_with_write_mode() {
    fd = open("test_file_read_empty.txt", O_WRONLY);
    char buf[BUF_SIZE];
    assert(read(fd, buf, BUF_SIZE) == -1);

    close(fd); // clean up the state for the next test
}

//========================== WRITE ==========================
int test_write_file_opened_with_write_mode() {
    fd = open("test_file_write.txt", O_WRONLY);
    char buf[BUF_SIZE] = "hello";
    assert(write(fd, buf, BUF_SIZE) == 5);

    close(fd); // clean up the state for the next test
}

int test_write_file_opened_with_readwrite_mode() {
    fd = open("test_file_write.txt", O_RDWR);
    char buf[BUF_SIZE];
    assert(write(fd, buf, BUF_SIZE) == 0);

    close(fd); // clean up the state for the next test
}

int test_write_file_opened_with_read_mode() {
    fd = open("test_file_write.txt", O_RDONLY);
    char buf[BUF_SIZE];
    assert(write(fd, buf, BUF_SIZE) == -1);

    close(fd); // clean up the state for the next test
}

int test_file_system() {
    // open console
    test_open_console();
    test_open_console_with_two_readers();
    test_open_console_with_multiple_writers();
    test_open_console_with_read_and_write();

    // open normal files
    test_open_non_existent_file();

    // read from file
    test_read_file_opened_with_read_mode();
    test_read_file_opened_with_readwrite_mode();
    test_read_file_opened_with_write_mode();

    // write to file
    // test_write_file_opened_with_write_mode();
    // file stat

    printf("File system test\tPassed\n");
}



