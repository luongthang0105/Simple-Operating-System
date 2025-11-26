#include <stdio.h>
#include <tests/file_system.h>
#include <tests/vm.h>
#include <tests/process.h>
#include <tests/macros.h>
#include <sos.h> 
#include <fcntl.h>
int main(void) {
    printf("❗❗Running SOS test suite...❗❗\n");
    printf("=============FILE SYSTEM============\n");
    test_file_system();

    printf("=============VIRTUAL MEMORY============\n");
    test_virtual_memory();

    // printf("=============PROCESS============\n");
    // test_process();
    
    printf(COLOR_GREEN "ALL TESTS PASSED ✅!\n" COLOR_RESET);
    // while(1);
    return 0;
}