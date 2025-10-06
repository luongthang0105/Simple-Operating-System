#pragma once
#include <networkconsole/networkconsole.h>

struct handler_sos_write_args {
    struct network_console* nwcs;
};
void handler_sos_write(void *args);


struct handler_sos_read_args {
    struct network_console* nwcs;
};
void handler_sos_read(void* args);