#include <sossharedapi/vfs.h>
#include <stdlib.h>
#include <fcntl.h>

void vfs_init(vfs_t *vfs) {
    for (int i = 0; i < PROCESS_MAX_FILES; i++) {
        vfs->fd_table[i].is_opened = false;
        vfs->fd_table[i].path = NULL;
        vfs->fd_table[i].mode = -1;
    }
    vfs->fd_table[CONSOLE_FD].is_opened = true;
    vfs->fd_table[CONSOLE_FD].path = "console";
    vfs->fd_table[CONSOLE_FD].mode = O_WRONLY;
}

int find_next_fd(vfs_t *vfs) {
    int fd = 4; // reserve 0,1,2 for stdin/out/err and 3 for network_console
    while (vfs->fd_table[fd].is_opened && fd < PROCESS_MAX_FILES) {
        fd += 1;
    }
    return fd;
}