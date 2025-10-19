
#pragma once
#include <sossharedapi/vfs.h>
#include <stdbool.h>
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
struct sos_fd {
    fmode_t mode;
    bool is_opened;
};
typedef struct sos_fd sos_fd_t;
/* file descriptor number is used to index to the array */
static sos_fd_t sos_fd_table[MAX_NUM_FILES];