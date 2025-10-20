
#pragma once
#include <stdbool.h>
#include <nfsc/libnfs.h>

/* file modes */
#define FM_EXEC  1
#define FM_WRITE 2
#define FM_READ  4
typedef int fmode_t;

/* file mode checks */
#define HAS_FM_READ(x)      ((x >> 2) & 1)
#define HAS_FM_WRITE(x)     ((x >> 1) & 1)
#define HAS_FM_EXEC(x)      (x & 1)

/* stat file types */
#define ST_FILE    1    /* plain file */
#define ST_SPECIAL 2    /* special (console) file */
typedef int st_type_t;

#define CONSOLE_FD 3    /* File descriptors 0,1,2 are already reserved for stdin, stdout and stderr */

typedef struct {
    st_type_t st_type;    /* file type */
    fmode_t   st_fmode;   /* access mode */
    unsigned  st_size;    /* file size in bytes */
    long      st_ctime;   /* Unix file creation time (ms) */
    long      st_atime;   /* Unix file last access (open) time (ms) */
} sos_stat_t;

#define MAX_NUM_FILES 10
typedef struct {
    fmode_t mode;
    bool is_opened;
    char *path;
    struct nfsfh* fh;
} sos_fd_t;

typedef struct {
    sos_fd_t fd_table[MAX_NUM_FILES];
} vfs_t;

/* file descriptor number is used to index to the array */
void vfs_init(vfs_t *vfs);
int find_next_fd(vfs_t *vfs);