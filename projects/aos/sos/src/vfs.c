#include <sossharedapi/vfs.h>
#include <stdlib.h>

void vfs_init(vfs_t *vfs) {
    for (int i = 0; i < PROCESS_MAX_FILES; i++) {
        vfs->fd_table[i].is_opened = false;
        vfs->fd_table[i].path = NULL;
        vfs->fd_table[i].mode = 0;
    }

    // Optional: mark 0, 1, 2 as "open" for terminal device
    vfs->fd_table[0].is_opened = true;
    vfs->fd_table[1].is_opened = true;
    vfs->fd_table[2].is_opened = true;
    vfs->fd_table[0].path = "stdin";
    vfs->fd_table[1].path = "stdout";
    vfs->fd_table[2].path = "stderr";
}

int find_next_fd(vfs_t *vfs) {
    int fd = 4; // reserve 0,1,2 for stdin/out/err and 3 for network_console
    while (vfs->fd_table[fd].is_opened && fd < PROCESS_MAX_FILES) {
        fd += 1;
    }
    return fd;
}