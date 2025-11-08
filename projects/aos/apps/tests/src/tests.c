#include <stdio.h>
#include "file_system/file_system.h"
#include "virtual_memory/vm.h"

int main(void) {
    printf("❗❗Running SOS test suite...❗❗\n");
    test_file_system();
    test_virtual_memory();
    return 0;
}