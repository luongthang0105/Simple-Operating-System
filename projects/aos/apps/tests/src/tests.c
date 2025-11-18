#include <stdio.h>
#include <tests/file_system.h>
#include <tests/vm.h>
#include <tests/macros.h>
#include <sos.h> 
#include <fcntl.h>
int main(void) {
    printf("❗❗Running SOS test suite...❗❗\n");
    printf("==========FILE SYSTEM============\n");
    test_file_system();

    printf("==========VIRTUAL MEMORY============\n");
    test_virtual_memory();

    printf(COLOR_GREEN "ALL TESTS PASSED ✅!\n" COLOR_RESET);
    return 0;
}