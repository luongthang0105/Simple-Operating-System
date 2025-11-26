
#pragma once
#include <stdbool.h>
#include <nfsc/libnfs.h>

/* file modes */
#define FM_EXEC  1
#define FM_WRITE 2
#define FM_READ  4
typedef int fmode_t;

/* stat file types */
#define ST_FILE    1    /* plain file */
#define ST_SPECIAL 2    /* special (console) file */
typedef int st_type_t;

#define STDIN_FD   0
#define STDOUT_FD  1
#define STDERR_FD  2
#define CONSOLE_FD 3
#define PROCESS_MAX_FILES 16

typedef struct {
    st_type_t st_type;    /* file type */
    fmode_t   st_fmode;   /* access mode */
    unsigned  st_size;    /* file size in bytes */
    long      st_ctime;   /* Unix file creation time (ms) */
    long      st_atime;   /* Unix file last access (open) time (ms) */
} sos_stat_t;

typedef struct {
    fmode_t mode;
    bool is_opened;
    char *path;
    struct nfsfh* fh;
} sos_fd_t;

typedef struct {
    sos_fd_t fd_table[PROCESS_MAX_FILES];
} vfs_t;

/* file descriptor number is used to index to the array */
int init_vfs(vfs_t **vfs);
int find_next_fd(vfs_t *vfs);
void destroy_vfs(vfs_t *vfs);