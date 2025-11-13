#include <stdio.h>
#include <tests/file_system.h>
#include <tests/vm.h>
#include <tests/macros.h>
#include <sos.h> 
#include <fcntl.h>
int main(void) {
    int fd = sos_open("console", O_WRONLY);
    printf("❗❗Running SOS test suite...❗❗\n");
    test_file_system();
    test_virtual_memory();
    printf(COLOR_GREEN "ALL TESTS PASSED ✅!\n" COLOR_RESET);
    sos_close(fd);
    return 0;
}