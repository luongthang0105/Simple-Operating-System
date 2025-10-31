/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */
#include <autoconf.h>
#include <utils/util.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#include <cspace/cspace.h>
#include <aos/sel4_zf_logif.h>
#include <aos/debug.h>

#include <clock/clock.h>
#include <cpio/cpio.h>
#include <elf/elf.h>
#include <networkconsole/networkconsole.h>

#include <sel4runtime.h>
#include <sel4runtime/auxv.h>

#include "bootstrap.h"
#include "irq.h"
#include "network.h"
#include "frame_table.h"
#include "drivers/uart.h"
#include "ut.h"
#include "vmem_layout.h"
#include "mapping.h"
#include "elfload.h"
#include "syscalls.h"
#include "tests.h"
#include "utils.h"
#include "threads.h"
#include <sos/gen_config.h>
#include <utils/sglib.h>
#include <utils/list.h>
#include <sossharedapi/syscalls.h>
#include "user_process.h"
#include "pagetable.h"
#include "vm_region.h"
#include <nfsc/libnfs.h>
#include "fcntl.h"
// #include "syscall_handlers/syscall_handlers.h"
#ifdef CONFIG_SOS_GDB_ENABLED
#include "debugger.h"
#endif /* CONFIG_SOS_GDB_ENABLED */

#include <aos/vsyscall.h>
#include "backtrace.h"
#include <nfsc/libnfs.h>

/*
 * To differentiate between signals from notification objects and and IPC messages,
 * we assign a badge to the notification object. The badge that we receive will
 * be the bitwise 'OR' of the notification object badge and the badges
 * of all pending IPC messages.
 *
 * All badged IRQs set high bit, then we use unique bits to
 * distinguish interrupt sources.
 */
#define IRQ_EP_BADGE         BIT(seL4_BadgeBits - 1ul)
#define IRQ_IDENT_BADGE_BITS MASK(seL4_BadgeBits - 1ul)

#define APP_NAME             "tests"
#define APP_PRIORITY         (0)
#define APP_EP_BADGE         (101)

/* The number of additional stack pages to provide to the initial
 * process */
#define INITIAL_PROCESS_STACK_PAGES 10
#define MAX_PROCESS_STACK_PAGES 9*29


/* Network console (nwcs) circular queue buffer */
#define DIM (size_t)8092
static char nwcs_buf[DIM];
static size_t i, j;
static int nwcs_reader = -1; // thread index that is currently the nwcs reader

/* The linker will link this symbol to the start address  *
 * of an archive of attached applications.                */
extern char _cpio_archive[];
extern char _cpio_archive_end[];
extern char __eh_frame_start[];
/* provided by gcc */
extern void (__register_frame)(void *);

/* root tasks cspace */
cspace_t cspace;

static seL4_CPtr sched_ctrl_start;
static seL4_CPtr sched_ctrl_end;

/* the one process we start */
static user_process_t user_process;

struct syscall_loop_args {
    seL4_CPtr ep;
    int thread_index;
};

struct sos_open_callback_private_data {
    int thread_index;
    int fd;
    int err;
};

struct sos_stat_callback_private_data {
    int thread_index;
    uintptr_t stat_buf_vaddr;
    st_type_t st_type;
    int err;
};

struct network_console *network_console;

#define MAX_WORKER_THREADS  1
static sos_thread_t* worker_threads[MAX_WORKER_THREADS];

/* Copy data from user app to SOS. Returns number of bytes copied. 
*/
static size_t copy_from_user(void* to, const void* from, size_t nbyte) {
    size_t rem_bytes = nbyte;
    size_t bytes_copied = 0;
    uintptr_t from_vaddr = (uintptr_t) from;
    
    char *temp = (char*) to;
    while (rem_bytes > 0) {
        frame_metadata_t *frame = find_frame(from_vaddr, user_process.page_global_directory);
        if (!frame) {
            ZF_LOGE("Unable to find a frame for buf_vaddr at %p", from_vaddr);
            return bytes_copied;
        }

        // source data of the "from" buf
        unsigned char* source_data = frame_data(frame->frame_ref);

        size_t offset = from_vaddr % PAGE_SIZE_4K;
        size_t max_bytes_to_copy = PAGE_SIZE_4K - offset;
        size_t bytes_to_copy = MIN(rem_bytes, max_bytes_to_copy);
        
        memcpy(&temp[bytes_copied], &source_data[offset], bytes_to_copy);

        rem_bytes -= bytes_to_copy;
        from_vaddr += bytes_to_copy;
        bytes_copied += bytes_to_copy;
    }
    return nbyte;
}


/* Copy data from SOS to user app. Returns number of bytes copied.
*/
static size_t copy_to_user(void* to, const void* from, size_t nbyte) {
    size_t rem_bytes = nbyte;
    size_t bytes_copied = 0;
    uintptr_t to_vaddr = (uintptr_t) to;
    while (rem_bytes > 0) {
        frame_metadata_t *frame = find_frame(to_vaddr, user_process.page_global_directory);
        if (!frame) {
            ZF_LOGI("vaddr %p is not mapped to any frame, trying to allocate frame...", (void*)to_vaddr);
            vm_region_t *valid_region = find_valid_region(to_vaddr, BIT(6), user_process.vm_regions);

            if (valid_region == NULL) {
                ZF_LOGE("vaddr %p resolves to an invalid region access", (void*)to_vaddr);
                return 0;
            }
            
            int result = allocate_new_frame(&cspace, to_vaddr, &user_process, valid_region->permission);
            if (result != 0) {
                ZF_LOGE("Unable to allocate a new frame at %p!\n", (void*)to_vaddr);
                return 0;
            }

            ZF_LOGI("Successfully allocate a new frame at %p", (void*)to_vaddr);
            continue;
        }

        // source data of the "to" buf
        unsigned char* source_data = frame_data(frame->frame_ref);

        size_t offset = to_vaddr % PAGE_SIZE_4K;
        size_t max_bytes_to_copy = PAGE_SIZE_4K - offset;
        size_t bytes_to_copy = MIN(rem_bytes, max_bytes_to_copy);
        
        char *temp = (char*) from;
        memcpy(&source_data[offset], &temp[bytes_copied], bytes_to_copy);

        rem_bytes -= bytes_to_copy;
        to_vaddr += bytes_to_copy;
        bytes_copied += bytes_to_copy;
    }
    return nbyte;
}
void sos_open_callback(int err, struct nfs_context *nfs, void *data, void *private_data) {
    struct sos_open_callback_private_data *ret_private_data =  (struct sos_open_callback_private_data *)private_data;

    int thread_index    = ret_private_data->thread_index;
    int fd              = ret_private_data->fd;

    if (err < 0) {
        ZF_LOGE("error: %d, error msg: %s\n", err, (char*)data);
        ret_private_data->err = err;
        seL4_Signal(worker_threads[thread_index]->ntfn);
        return;
    }
    
    struct nfsfh *nfsfh = (struct nfsfh *)data;
    user_process.vfs->fd_table[fd].fh = nfsfh;

    seL4_Signal(worker_threads[thread_index]->ntfn);
}

void sos_stat_callback(int err, struct nfs_context *nfs, void *data, void *private_data) {
    struct sos_stat_callback_private_data *ret_private_data =  (struct sos_stat_callback_private_data *)private_data;

    int thread_index            = ret_private_data->thread_index;
    uintptr_t stat_buf_vaddr    = ret_private_data->stat_buf_vaddr;

    if (err < 0) {
        ZF_LOGE("error: %d, error msg: %s\n", err, (char*)data);
        ret_private_data->err = err;
        seL4_Signal(worker_threads[thread_index]->ntfn);
        return;
    }
   
    struct nfs_stat_64 *nfs_stat = (struct nfs_stat_64 *)data;
    sos_stat_t *sos_stat = malloc(sizeof(sos_stat_t));
    sos_stat->st_type   = ret_private_data->st_type;
    
    
    sos_stat->st_fmode  = 0;
    if (nfs_stat->nfs_mode & S_IRUSR) {
        sos_stat->st_fmode |= FM_READ;
    }
    if (nfs_stat->nfs_mode & S_IWUSR) {
        sos_stat->st_fmode |= FM_WRITE;
    }
    if (nfs_stat->nfs_mode & S_IXUSR) {
        sos_stat->st_fmode |= FM_EXEC;
    }

    sos_stat->st_size   = nfs_stat->nfs_size;
    sos_stat->st_ctime  = nfs_stat->nfs_ctime;
    sos_stat->st_atime  = nfs_stat->nfs_atime;

    copy_to_user((void *)stat_buf_vaddr, sos_stat, sizeof(sos_stat_t));
    seL4_Signal(worker_threads[thread_index]->ntfn);
}

int handler_sos_open_nwcs(fmode_t mode) {
    sos_fd_t *console = &user_process.vfs->fd_table[CONSOLE_FD];

    if (!console->is_opened) {
        console->mode = mode;
        console->path = "console";
        console->is_opened = true;
        return CONSOLE_FD;
    }

    bool has_reader = (console->mode == O_RDONLY || console->mode == O_RDWR);
    switch (mode) {
        case O_RDONLY:
        case O_RDWR:
            if (has_reader) return -1; // only allow one nwcs reader
            console->mode = O_RDWR;
            break;
        case O_WRONLY:
            console->mode = has_reader ? O_RDWR : O_WRONLY;
            break;
    }
    
    console->path = "console";
    console->is_opened = true;
    return CONSOLE_FD;
}

void handler_sos_stat(seL4_MessageInfo_t *reply_msg, int thread_index) {
    ZF_LOGE("syscall: stat!\n");
    *reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);

    uintptr_t path_vaddr        = seL4_GetMR(1);
    int path_len                = seL4_GetMR(2) + 1; // includes terminator
    uintptr_t stat_buf_vaddr    = seL4_GetMR(3);

    char *temp_path_buf = malloc(path_len);
    size_t nbyte = copy_from_user(temp_path_buf, path_vaddr, path_len);

    if (strcmp(temp_path_buf, "..") == 0) {
        free(temp_path_buf);
        temp_path_buf = malloc(strlen(".") + 1);
        strcpy(temp_path_buf, ".");
    }

    struct nfs_context *nfs_context = get_nfs_context();
    struct sos_stat_callback_private_data *private_data = malloc(sizeof(struct sos_stat_callback_private_data));
    private_data->thread_index      = thread_index;
    private_data->stat_buf_vaddr    = stat_buf_vaddr;
    private_data->st_type           = strcmp(temp_path_buf, "console") == 0 ? ST_SPECIAL : ST_FILE;
    private_data->err               = 0;

    int err = nfs_stat64_async(nfs_context, temp_path_buf, sos_stat_callback, private_data);
    if (err < 0) {
        ZF_LOGE("An error occured when trying to queue the command nfs_stat64_async. The callback will not be invoked.");
        free(temp_path_buf);
        free(private_data);
        seL4_SetMR(0, -1);
        return;
    }

    seL4_Wait(worker_threads[thread_index]->ntfn, NULL);

    err = private_data->err;
    
    free(private_data);
    free(temp_path_buf);

    if (err) {
        seL4_SetMR(0, -1);
    } else {
        seL4_SetMR(0, 0);
    }
}   

void handler_sos_open(seL4_MessageInfo_t *reply_msg, int thread_index) {
    ZF_LOGE("syscall: open!\n");
    *reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);

    uintptr_t path_vaddr    = seL4_GetMR(1);
    int path_len            = seL4_GetMR(2) + 1; // now includes null terminator
    fmode_t mode            = seL4_GetMR(3);

    unsigned char *path_data = find_frame_data(path_vaddr, user_process.page_global_directory);
    if (!path_data) {
        seL4_SetMR(0, -1);
        return;
    }

    char *temp_path_buf = malloc(path_len);
    if (temp_path_buf == NULL) {
        ZF_LOGE("Failed to allocate memory for temp_path_buf");
        seL4_SetMR(0, -1);
        return;        
    }

    size_t nbyte = copy_from_user(temp_path_buf, path_vaddr, path_len);

    if (strcmp(temp_path_buf, "console") == 0) {
        free(temp_path_buf);
        seL4_SetMR(0, handler_sos_open_nwcs(mode));
        return;
    }

    int fd = find_next_fd(user_process.vfs);

    if (fd >= PROCESS_MAX_FILES) {
        ZF_LOGE("Unable to allocate a new file descriptor since the number of open files exceeded %d\n", PROCESS_MAX_FILES);
        free(temp_path_buf);
        seL4_SetMR(0, -1);
        return;
    }

    struct nfs_context *nfs_context = get_nfs_context();
    struct sos_open_callback_private_data *private_data = malloc(sizeof(struct sos_open_callback_private_data));
    if (private_data == NULL) {
        ZF_LOGE("Failed to allocate memory for nfs_open callback private_data");
        seL4_SetMR(0, -1);
        free(temp_path_buf);
        return;        
    }

    private_data->thread_index  = thread_index;
    private_data->fd            = fd;
    private_data->err           = 0;

    int err = nfs_open_async(nfs_context, temp_path_buf, mode | O_CREAT, sos_open_callback, private_data);

    if (err) {
        ZF_LOGE("An error occured when trying to queue the command nfs_open_async. The callback will not be invoked.");
        free(temp_path_buf);
        free(private_data);
        seL4_SetMR(0, -1);
        return;
    }

    seL4_Wait(worker_threads[thread_index]->ntfn, NULL);
    err = private_data->err;
    free(private_data);

    if (err) {
        free(temp_path_buf);
        seL4_SetMR(0, -1);
        return;
    }

    user_process.vfs->fd_table[fd].is_opened = true;
    user_process.vfs->fd_table[fd].mode = mode;
    user_process.vfs->fd_table[fd].path = temp_path_buf;

    seL4_SetMR(0, fd);
}

typedef struct {
    size_t thread_index;
    int status;
} nfs_close_cb_args_t;

void nfs_close_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    if (status < 0) {
        ZF_LOGE("nfs_close failed with error: %s\n", (char*)data);
        return;
    }

    nfs_close_cb_args_t *args = private_data;
    size_t thread_index = args->thread_index;
    args->status = status;

    seL4_Signal(worker_threads[thread_index]->ntfn);
    return;
}

void handler_sos_close(seL4_MessageInfo_t *reply_msg, int thread_index) {
    ZF_LOGE("syscall: close!\n");
    *reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);

    size_t fd = seL4_GetMR(1);    
    
    if (fd < 0 || fd >= PROCESS_MAX_FILES) {
        ZF_LOGE("Invalid file descriptor");
        seL4_SetMR(0, -1);
        return;
    }

    if (user_process.vfs->fd_table[fd].is_opened == false) {
        ZF_LOGE("File is not opened");
        seL4_SetMR(0, -1);
        return;
    }

    if (fd == CONSOLE_FD) {
        user_process.vfs->fd_table[fd].is_opened = false;
        user_process.vfs->fd_table[fd].mode = -1;
        free(user_process.vfs->fd_table[fd].path);
        seL4_SetMR(0, 0);
        return;
    }

    struct nfs_context* nfs_context = get_nfs_context();
    nfs_close_cb_args_t args = {.thread_index = thread_index};
    int ret = nfs_close_async(nfs_context, user_process.vfs->fd_table[fd].fh, nfs_close_cb, (void*)&args);
    if (ret < 0) {
        ZF_LOGE("Failed to queue nfs_close_async");
        seL4_SetMR(0, -1);
        return;
    }

    seL4_Wait(worker_threads[thread_index]->ntfn, NULL);

    if (args.status < 0) {
        seL4_SetMR(0, -1);
        return;
    }

    // Mark fd slot as free. Don't need to free (struct nfsfh*) because it has been freed in nfs_close_async.
    user_process.vfs->fd_table[fd].is_opened = false;
    user_process.vfs->fd_table[fd].mode = -1;
    free(user_process.vfs->fd_table[fd].path);
    seL4_SetMR(0, 0);
    return;
}

typedef struct {
    size_t thread_index;
    size_t bytes_written;
} nfs_write_cb_args_t;

void nfs_write_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    if (status < 0) {
        ZF_LOGE("nfs_write failed with error: %s\n", (char*)data);
        return;
    }

    nfs_write_cb_args_t *args = private_data;
    size_t thread_index = args->thread_index;
    args->bytes_written = status;

    seL4_Signal(worker_threads[thread_index]->ntfn);
    return;
}
#define BREAKDOWN_THRESHOLD 70000

void handler_sos_write(seL4_MessageInfo_t *reply_msg, size_t thread_index) {
    ZF_LOGV("syscall: write!\n");

    *reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
    
    uintptr_t buf_vaddr     = seL4_GetMR(1);
    size_t nbytes           = seL4_GetMR(2);
    size_t file_desc        = seL4_GetMR(3);
    
    if (nbytes >= BREAKDOWN_THRESHOLD) {
        ZF_LOGE("nbytes to write is too large!");
        seL4_SetMR(0, -1);
        return;
    }

    if (file_desc < 0 || file_desc >= PROCESS_MAX_FILES) {
        ZF_LOGE("File descriptor is invalid.");
        seL4_SetMR(0, -1);
        return;
    }

    if (!user_process.vfs->fd_table[file_desc].is_opened) {
        ZF_LOGE("File is not open yet!");
        seL4_SetMR(0, -1);
        return;
    }

    if (user_process.vfs->fd_table[file_desc].mode != O_WRONLY && 
        user_process.vfs->fd_table[file_desc].mode != O_RDWR) {
        ZF_LOGE("File %d is not open to write!", file_desc);
        seL4_SetMR(0, -1);
        return;
    }

    char* temp_buf = malloc(nbytes);
    if (temp_buf == NULL) {
        ZF_LOGE("Failed to allocate memory for temp_buf");
        seL4_SetMR(0, -1);
        return;        
    }

    size_t bytes_copied_from_user = copy_from_user(temp_buf, (void*)buf_vaddr, nbytes);
    if (bytes_copied_from_user == 0) {
        free(temp_buf);
        ZF_LOGE("Failed to copy from user");
        seL4_SetMR(0, -1);
        return; 
    }

    if (file_desc != CONSOLE_FD) { /* normal files */
        struct nfs_context *nfs_context = get_nfs_context();

        nfs_write_cb_args_t args = {.thread_index = thread_index};
        int ret = nfs_write_async(nfs_context, user_process.vfs->fd_table[file_desc].fh, nbytes, temp_buf,
                        nfs_write_cb, (void*)&args);
        if (ret < 0) {
            ZF_LOGE("Failed to queue nfs_write_async");
            seL4_SetMR(0, -1);
            free(temp_buf);
            return;
        }
        seL4_Wait(worker_threads[thread_index]->ntfn, NULL);

        size_t bytes_written = args.bytes_written;
        seL4_SetMR(0, bytes_written);
    } else { /* console file, send it to network console */
        int bytes_sent = network_console_send(network_console, temp_buf, nbytes);
        if (bytes_sent == -1) {
            ZF_LOGE("Failed to send %lu bytes via network_console_send", nbytes);
            seL4_SetMR(0, -1);
        } else {
            seL4_SetMR(0, bytes_sent);
        }
    }
    free(temp_buf);
}

typedef struct {
    size_t thread_index;
    size_t bytes_read;
    void *user_buf_vaddr;
} nfs_read_cb_args_t;

void nfs_read_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    if (status < 0) {
        ZF_LOGE("nfs_read failed with error: %s\n", (char*)data);
        return;
    }

    nfs_read_cb_args_t *args = private_data;

    args->bytes_read = copy_to_user(args->user_buf_vaddr, data, status);

    seL4_Signal(worker_threads[args->thread_index]->ntfn);
    return;
}
/*  nwcs_reader must be set to -1 before the function retunrs. 
    write_to_buf() will signal when nwcs_reader != -1, so not setting it back to -1
    will make write_to_buf() signals the syscall loop instead.
*/
void handler_sos_read(seL4_MessageInfo_t *reply_msg, int thread_index) {
    ZF_LOGV("syscall: read!\n");
    *reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);

    uintptr_t buf_vaddr     = seL4_GetMR(1);
    size_t nbytes           = seL4_GetMR(2);
    int file_desc           = seL4_GetMR(3);

    if (nbytes >= BREAKDOWN_THRESHOLD) {
        ZF_LOGE("nbytes to read is too large!");
        seL4_SetMR(0, -1);
        return;
    }

    if (file_desc < 0 || file_desc >= PROCESS_MAX_FILES) {
        ZF_LOGE("File descriptor is invalid.");
        seL4_SetMR(0, -1);
        return;
    }

    if (!user_process.vfs->fd_table[file_desc].is_opened) {
        ZF_LOGE("File is not open yet!");
        seL4_SetMR(0, -1);
        return;
    }

    if (user_process.vfs->fd_table[file_desc].mode != O_RDONLY && 
        user_process.vfs->fd_table[file_desc].mode != O_RDWR) {
        ZF_LOGE("File %d is not open to read!", file_desc);
        seL4_SetMR(0, -1);
        return;
    }

    if (file_desc != CONSOLE_FD) { /* normal files */
        if (user_process.vfs->fd_table[file_desc].fh == NULL) {
            ZF_LOGE("NFS file handle for fd=%d does not exist", file_desc);
            seL4_SetMR(0, -1);
            return;
        }
        struct nfs_context *nfs_context = get_nfs_context();

        nfs_read_cb_args_t args = {.thread_index = thread_index, .user_buf_vaddr = buf_vaddr};
        int ret = nfs_read_async(nfs_context, user_process.vfs->fd_table[file_desc].fh, nbytes,
                        nfs_read_cb, (void*)&args);
        if (ret < 0) {
            ZF_LOGE("Failed to queue nfs_read_async");
            seL4_SetMR(0, -1);
            return;
        }
        seL4_Wait(worker_threads[thread_index]->ntfn, NULL);

        size_t bytes_read = args.bytes_read;
        seL4_SetMR(0, bytes_read);
        return;
    } else {
        char* temp_buf = malloc(nbytes);
        size_t remaining_bytes = nbytes;
        size_t bytes_read = 0;
        while (remaining_bytes > 0) {
            if (SGLIB_QUEUE_IS_EMPTY(char, nwcs_buf, i, j)) {
                nwcs_reader = thread_index;
                seL4_Wait(worker_threads[thread_index]->ntfn, NULL);
            }

            temp_buf[bytes_read] = SGLIB_QUEUE_FIRST_ELEMENT(char, nwcs_buf, i, j);
            SGLIB_QUEUE_DELETE_FIRST(char, nwcs_buf, i, j, DIM);
            if (temp_buf[bytes_read] == '\n') {
                bytes_read++;
                break;
            }

            remaining_bytes--;
            bytes_read++;
        }
        nwcs_reader = -1;
        seL4_SetMR(0, bytes_read);

        copy_to_user(buf_vaddr, temp_buf, nbytes);

        free(temp_buf);
        return;
    }
}

void handler_sos_timestamp(seL4_MessageInfo_t *reply_msg) {
    ZF_LOGV("syscall: get timestamp!\n");
    *reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, get_time());
}

/* Callback for timer registered by usleep system call */
void timeout_callback(uint32_t id, void *data) {
    int thread_index = *(int*)data;
    seL4_Signal(worker_threads[thread_index]->ntfn);
}

void handler_sos_usleep(seL4_MessageInfo_t *reply_msg, int thread_index) {
    ZF_LOGV("syscall: usleep!\n");
    int msec = seL4_GetMR(1);

    *reply_msg = seL4_MessageInfo_new(0, 0, 0, 0);

    register_timer(msec, timeout_callback, &thread_index);
    seL4_Wait(worker_threads[thread_index]->ntfn, NULL);
}

void handler_sos_brk(seL4_MessageInfo_t *reply_msg) {
    ZF_LOGV("syscall: brk!\n");
    *reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);

    uintptr_t new_brk = seL4_GetMR(1);
    if (new_brk == 0) {
        seL4_SetMR(0, user_process.heap_region->vaddr_base);
        return;
    }

    /* check if the new_brk is valid: 
        - within the heap & stack bottom
    */
    
    uintptr_t guard_page_vaddr = user_process.guard_page_vaddr;
    uintptr_t heap_base = user_process.heap_region->vaddr_base;
    uintptr_t curr_brk = heap_base + user_process.heap_region->size;

    if (new_brk < heap_base || new_brk > guard_page_vaddr) {
        ZF_LOGE("New program break is not valid");
        seL4_SetMR(0, 0);
        return;
    }

    if (curr_brk == new_brk) {
        seL4_SetMR(0, new_brk);
        return;
    } else if (curr_brk < new_brk) { /* allocate more memory */
        uintptr_t next_page_vaddr_to_alloc = ROUND_UP(curr_brk, PAGE_SIZE_4K);
    
        while (next_page_vaddr_to_alloc < new_brk) {
            int result = allocate_new_frame(&cspace, next_page_vaddr_to_alloc, &user_process, user_process.heap_region->permission);
            if (result != 0) {
                ZF_LOGE("Unable to allocate a new frame at %p!\n", (void*)next_page_vaddr_to_alloc);
                seL4_SetMR(0, 0);
                return;
            }
            next_page_vaddr_to_alloc += PAGE_SIZE_4K;
        }
    } else { /* deallocate frames */
        uintptr_t next_page_vaddr_to_dealloc = ROUND_DOWN(curr_brk, PAGE_SIZE_4K);
        
        while (next_page_vaddr_to_dealloc >= new_brk) {
            int ret = sos_shadow_unmap_frame(next_page_vaddr_to_dealloc, user_process.page_global_directory, &cspace);
            if (ret == -1) {
                seL4_SetMR(0, 0);
                return;
            }
            next_page_vaddr_to_dealloc -= PAGE_SIZE_4K;
        }
    }

    user_process.heap_region->size = new_brk - heap_base;
    seL4_SetMR(0, new_brk);
    return;
}

typedef struct nfs_opendir_cb_args {
    int thread_index;
} nfs_opendir_cb_args_t;

void nfs_opendir_cb(int status, struct nfs_context *nfs, void *data, void *private_data) {
    if (status < 0) {
        user_process.curr_dir = NULL;
        ZF_LOGE("nfs_opendir failed with error: %s\n", (char*)data);
        return;
    }

    user_process.curr_dir = (struct nfsdir*) data;
    
    int thread_index = ((nfs_opendir_cb_args_t*)private_data)->thread_index;
    seL4_Signal(worker_threads[thread_index]->ntfn);
    
    return;
}

void handler_sos_getdirent(seL4_MessageInfo_t *reply_msg, int thread_index) {
    ZF_LOGV("syscall: getdirent!\n");
    *reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
   
    size_t pos = seL4_GetMR(1);
    uintptr_t buf_vaddr = seL4_GetMR(2);
    size_t nbyte = seL4_GetMR(3);

    struct nfs_context* nfs_context = get_nfs_context();
    
    // calls opendir to get struct nfsdir*
    nfs_opendir_cb_args_t args = {.thread_index = thread_index};
    int ret = nfs_opendir_async(nfs_context, "./", nfs_opendir_cb, (void*) &args);
    if (ret < 0) {
        ZF_LOGE("Failed to queue nfs_opendir_async");
        seL4_SetMR(0, -1);
        return;
    }

    seL4_Wait(worker_threads[thread_index]->ntfn, NULL);

    // calls nfs_readdir to read the expected entry (struct dirent*)
    struct nfsdirent *nfsdirent;
    for (size_t i = 0; i <= pos; ++i) {
        nfsdirent = nfs_readdir(nfs_context, user_process.curr_dir);
        if (nfsdirent == NULL) {
            if (i == pos) { // pos is right after the last entry
                seL4_SetMR(0, 0);
                return;
            } else { // otherwise, treat this as non-existent entry 
                seL4_SetMR(0, -1);
                return;
            }
        }
    }
    // gets the name field, and copy it to the name buf (including null terminator)
    size_t bytes_copied = copy_to_user((void*) buf_vaddr, (void*)nfsdirent->name, MIN(nbyte, strlen(nfsdirent->name) + 1));

    seL4_SetMR(0, bytes_copied);
    return;
}

/**
 * Deals with a syscall and sets the message registers before returning the
 * message info to be passed through to seL4_ReplyRecv()
 */
seL4_MessageInfo_t handle_syscall(UNUSED seL4_Word badge, UNUSED int num_args, bool *have_reply, int thread_index)
{
    seL4_MessageInfo_t reply_msg;

    /* get the first word of the message, which in the SOS protocol is the number
     * of the SOS "syscall". */
    seL4_Word syscall_number = seL4_GetMR(0);

    /* Set the reply flag */
    *have_reply = true;

    /* Process system call */
    /* Ideally, put all of these into syscall_handlers.c (in the future :P)*/
    switch (syscall_number) {
    case SYSCALL_SOS_OPEN:
        handler_sos_open(&reply_msg, thread_index);
        break;
    case SYSCALL_SOS_CLOSE:
        handler_sos_close(&reply_msg, thread_index);
        break;
    case SYSCALL_SOS_WRITE:
        handler_sos_write(&reply_msg, thread_index);
        break;
    case SYSCALL_SOS_READ:
        handler_sos_read(&reply_msg, thread_index);
        break;
    case SYSCALL_SOS_TIMESTAMP:
        handler_sos_timestamp(&reply_msg);
        break;
    case SYSCALL_SOS_USLEEP:
        handler_sos_usleep(&reply_msg, thread_index);   
        break;
    case SYSCALL_SOS_BRK:
        handler_sos_brk(&reply_msg);
        break;
    case SYSCALL_SOS_GETDIRENT:
        handler_sos_getdirent(&reply_msg, thread_index);
        break;
    case SYSCALL_SOS_STAT:
        handler_sos_stat(&reply_msg, thread_index);
        break;
    default:
        reply_msg = seL4_MessageInfo_new(0, 0, 0, 0);
        ZF_LOGE("Unknown syscall %lu\n", syscall_number);
        /* Don't reply to an unknown syscall */
        *have_reply = false;
    }

    return reply_msg;
}

void write_to_buf(UNUSED struct network_console *network_console, char c) {
    bool is_empty_before = SGLIB_QUEUE_IS_EMPTY(char, nwcs_buf, i, j);
    SGLIB_QUEUE_ADD(char, nwcs_buf, c, i, j, DIM);
    if (is_empty_before && nwcs_reader != -1) {
        seL4_Signal(worker_threads[nwcs_reader]->ntfn);
    }
}
void handle_vm_fault(seL4_Fault_t fault, seL4_MessageInfo_t *reply_msg, bool *have_reply) {
    uintptr_t faultaddr = ROUND_DOWN(seL4_Fault_VMFault_get_Addr(fault), PAGE_SIZE_4K);
    seL4_Uint64 fsr = seL4_Fault_VMFault_get_FSR(fault);
    *reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
    vm_region_t *valid_region = find_valid_region(faultaddr, fsr, user_process.vm_regions);
    if (valid_region == NULL) {
        ZF_LOGE("Fault address %p resolves to an invalid region access", (void*)faultaddr);
        *have_reply = false; // don't reply to the user process if the fault vaddr is invalid
        return;
    }
    
    int result = allocate_new_frame(&cspace, faultaddr, &user_process, valid_region->permission);
    if (result != 0) {
        ZF_LOGE("Unable to allocate a new frame at %p!\n", (void*)faultaddr);
        *have_reply = false;
        return;
    }

    *have_reply = true;
    seL4_SetMR(0, 0);
    return;
}

seL4_MessageInfo_t handle_fault(seL4_MessageInfo_t tag, bool *have_reply) {
    seL4_MessageInfo_t reply_msg;
    seL4_Fault_t fault = seL4_getFault(tag);
    seL4_Uint64 fault_type = seL4_Fault_get_seL4_FaultType(fault);

    switch (fault_type) {
        case seL4_Fault_VMFault:
            handle_vm_fault(fault, &reply_msg, have_reply);
            break;
        default:
            /* some kind of fault */
            debug_print_fault(tag, APP_NAME);
            /* dump registers too */
            debug_dump_registers(user_process.tcb);

            reply_msg = seL4_MessageInfo_new(0, 0, 0, 0);
            ZF_LOGE("Unknown fault %lu\n", fault_type);
            /* Don't reply to an unknown fault */
            *have_reply = false;
            break;
    }
    return reply_msg;
}


NORETURN void syscall_loop(void* arg)
{
    seL4_CPtr reply;    
    seL4_CPtr ep = ((struct syscall_loop_args*)arg)->ep;
    int thread_index = ((struct syscall_loop_args*)arg)->thread_index;

    /* Create reply object */
    ut_t *reply_ut = alloc_retype(&reply, seL4_ReplyObject, seL4_ReplyBits);
    if (reply_ut == NULL) {
        ZF_LOGF("Failed to alloc reply object ut");
    }

    bool have_reply = false;
    seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0, 0, 0, 0);

    while (1) {
        seL4_Word badge = 0;
        seL4_MessageInfo_t message;
        /* Reply (if there is a reply) and block on ep, waiting for an IPC
         * sent over ep, or a notification from our bound notification object */
        if (have_reply) {
            message = seL4_ReplyRecv(ep, reply_msg, &badge, reply);
        } else {
            message = seL4_Recv(ep, &badge, reply);
        }
        /* Awake! We got a message - check the label and badge to
         * see what the message is about */
        seL4_Word label = seL4_MessageInfo_get_label(message);
        if (badge & IRQ_EP_BADGE) {
            /* It's a notification from our bound notification
             * object! */
            sos_handle_irq_notification(&badge, &have_reply);
        } else if (label == seL4_Fault_NullFault) {
            /* It's not a fault or an interrupt, it must be an IPC
             * message from console_test! */
            reply_msg = handle_syscall(badge, seL4_MessageInfo_get_length(message) - 1, &have_reply, thread_index);
        } else {
            /* Handle the fault */
            reply_msg = handle_fault(message, &have_reply);
        }
    }
}

static int stack_write(seL4_Word *mapped_stack, int index, uintptr_t val)
{
    mapped_stack[index] = val;
    return index - 1;
}

/* set up System V ABI compliant stack, so that the process can
 * start up and initialise the C library */
static uintptr_t init_process_stack(cspace_t *cspace, seL4_CPtr local_vspace, elf_t *elf_file)
{
    /* virtual addresses in the target process' address space */
    uintptr_t stack_top = PROCESS_STACK_TOP;
    uintptr_t stack_bottom = PROCESS_STACK_TOP - PAGE_SIZE_4K;
    /* virtual addresses in the SOS's address space */
    void *local_stack_top  = (seL4_Word *) SOS_SCRATCH;
    uintptr_t local_stack_bottom = SOS_SCRATCH - PAGE_SIZE_4K;

    /* find the vsyscall table */
    uintptr_t *sysinfo = (uintptr_t *) elf_getSectionNamed(elf_file, "__vsyscall", NULL);
    if (!sysinfo || !*sysinfo) {
        ZF_LOGE("could not find syscall table for c library");
        return 0;
    }

    /* allocate a stack frame for the user application*/
    seL4_Error err = allocate_new_frame(cspace, stack_bottom, &user_process, seL4_ReadWrite);
    frame_metadata_t *frame_metadata = find_frame(stack_bottom, user_process.page_global_directory);
    user_process.stack = frame_metadata->frame_cap;

    /* allocate a slot to duplicate the stack frame cap so we can map it into our address space */
    seL4_CPtr local_stack_cptr = cspace_alloc_slot(cspace);
    if (local_stack_cptr == seL4_CapNull) {
        ZF_LOGE("Failed to alloc slot for stack");
        return 0;
    }

    /* copy the stack frame cap into the slot */
    err = cspace_copy(cspace, local_stack_cptr, cspace, user_process.stack, seL4_AllRights);
    if (err != seL4_NoError) {
        cspace_free_slot(cspace, local_stack_cptr);
        ZF_LOGE("Failed to copy cap");
        return 0;
    }

    /* map it into the sos address space */
    err = map_frame(cspace, local_stack_cptr, local_vspace, local_stack_bottom, seL4_AllRights,
                    seL4_ARM_Default_VMAttributes);
    if (err != seL4_NoError) {
        cspace_delete(cspace, local_stack_cptr);
        cspace_free_slot(cspace, local_stack_cptr);
        return 0;
    }

    int index = -2;

    /* null terminate the aux vectors */
    index = stack_write(local_stack_top, index, 0);
    index = stack_write(local_stack_top, index, 0);

    /* write the aux vectors */
    index = stack_write(local_stack_top, index, PAGE_SIZE_4K);
    index = stack_write(local_stack_top, index, AT_PAGESZ);

    index = stack_write(local_stack_top, index, *sysinfo);
    index = stack_write(local_stack_top, index, AT_SYSINFO);

    index = stack_write(local_stack_top, index, PROCESS_IPC_BUFFER);
    index = stack_write(local_stack_top, index, AT_SEL4_IPC_BUFFER_PTR);

    /* null terminate the environment pointers */
    index = stack_write(local_stack_top, index, 0);

    /* we don't have any env pointers - skip */

    /* null terminate the argument pointers */
    index = stack_write(local_stack_top, index, 0);

    /* no argpointers - skip */

    /* set argc to 0 */
    stack_write(local_stack_top, index, 0);

    /* adjust the initial stack top */
    stack_top += (index * sizeof(seL4_Word));

    /* the stack *must* remain aligned to a double word boundary,
     * as GCC assumes this, and horrible bugs occur if this is wrong */
    assert(index % 2 == 0);
    assert(stack_top % (sizeof(seL4_Word) * 2) == 0);

    /* unmap our copy of the stack */
    err = seL4_ARM_Page_Unmap(local_stack_cptr);
    assert(err == seL4_NoError);

    /* delete the copy of the stack frame cap */
    err = cspace_delete(cspace, local_stack_cptr);
    assert(err == seL4_NoError);

    /* mark the slot as free */
    cspace_free_slot(cspace, local_stack_cptr);

    /* Exend the stack with extra pages */
    for (int page = 0; page < INITIAL_PROCESS_STACK_PAGES; page++) {
        stack_bottom -= PAGE_SIZE_4K;
        int result = allocate_new_frame(cspace, stack_bottom, &user_process, seL4_ReadWrite);
        if (result != 0) {
            ZF_LOGE("Unable to allocate a new frame at %p!\n", (void*)stack_bottom);
            return 0;
        }
    }
    /* Create a stack region */
    user_process.stack_region = add_vm_region(user_process.vm_regions, stack_top, MAX_PROCESS_STACK_PAGES * PAGE_SIZE_4K, seL4_ReadWrite, true);
    if (user_process.stack_region == NULL) {
        ZF_LOGE("Unable to add stack region");
        return 0;
    }
    user_process.guard_page_vaddr = stack_bottom - PAGE_SIZE_4K;
    return stack_top;
}

/* Start the first process, and return true if successful
 *
 * This function will leak memory if the process does not start successfully.
 * TODO: avoid leaking memory once you implement real processes, otherwise a user
 *       can force your OS to run out of memory by creating lots of failed processes.
 */
bool start_first_process(char *app_name, seL4_CPtr ep)
{
    /* Create a VSpace */
    user_process.vspace_ut = alloc_retype(&user_process.vspace, seL4_ARM_PageGlobalDirectoryObject,
                                              seL4_PGDBits);
    if (user_process.vspace_ut == NULL) {
        return false;
    }

    /* assign the vspace to an asid pool */
    seL4_Word err = seL4_ARM_ASIDPool_Assign(seL4_CapInitThreadASIDPool, user_process.vspace);
    if (err != seL4_NoError) {
        ZF_LOGE("Failed to assign asid pool");
        return false;
    }

    /* Create a simple 1 level CSpace */
    err = cspace_create_one_level(&cspace, &user_process.cspace);
    if (err != CSPACE_NOERROR) {
        ZF_LOGE("Failed to create cspace");
        return false;
    }
    
    /* Initialise the virtual file system */
    user_process.vfs = malloc(sizeof(vfs_t));
    vfs_init(user_process.vfs);

    /* Initialise a linked list of frame refs */
    user_process.page_global_directory = create_pgd();
    if (!user_process.page_global_directory) {
        ZF_LOGE("Failed to alloc page global directory");
        return false;
    }

    /* Initialise a linked list of vm_regions */
    user_process.vm_regions = malloc(sizeof(list_t));
    if (!user_process.vm_regions) {
        ZF_LOGE("Failed to alloc vm regions");
        return false;
    }
    list_init(user_process.vm_regions);

    /* Create an IPC buffer */
    err = allocate_new_frame(&cspace, PROCESS_IPC_BUFFER, &user_process, seL4_AllRights);
    if (err != 0) {
        ZF_LOGE("Unable to map IPC buffer for user app");
        return false;
    }

    /* Keep track of IPC buffer region */
    vm_region_t *ipc_region = add_vm_region(user_process.vm_regions, PROCESS_IPC_BUFFER, PAGE_SIZE_4K, seL4_AllRights, false);
    if (ipc_region == NULL) {
        ZF_LOGE("Unable to add ipc region");
        return false;
    }

    /* Saves the IPC buffer capability */
    frame_metadata_t *frame_metadata = find_frame(PROCESS_IPC_BUFFER, user_process.page_global_directory);
    user_process.ipc_buffer = frame_metadata->frame_cap;

    /* allocate a new slot in the target cspace which we will mint a badged endpoint cap into --
     * the badge is used to identify the process, which will come in handy when you have multiple
     * processes. */
    seL4_CPtr user_ep = cspace_alloc_slot(&user_process.cspace);
    if (user_ep == seL4_CapNull) {
        ZF_LOGE("Failed to alloc user ep slot");
        return false;
    }

    /* now mutate the cap, thereby setting the badge */
    err = cspace_mint(&user_process.cspace, user_ep, &cspace, ep, seL4_AllRights, APP_EP_BADGE);
    if (err) {
        ZF_LOGE("Failed to mint user ep");
        return false;
    }

    /* Create a new TCB object */
    user_process.tcb_ut = alloc_retype(&user_process.tcb, seL4_TCBObject, seL4_TCBBits);
    if (user_process.tcb_ut == NULL) {
        ZF_LOGE("Failed to alloc tcb ut");
        return false;
    }

    /* Configure the TCB */
    err = seL4_TCB_Configure(user_process.tcb,
                             user_process.cspace.root_cnode, seL4_NilData,
                             user_process.vspace, seL4_NilData, PROCESS_IPC_BUFFER,
                             user_process.ipc_buffer);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to configure new TCB");
        return false;
    }

    /* Create scheduling context */
    user_process.sched_context_ut = alloc_retype(&user_process.sched_context, seL4_SchedContextObject,
                                                     seL4_MinSchedContextBits);
    if (user_process.sched_context_ut == NULL) {
        ZF_LOGE("Failed to alloc sched context ut");
        return false;
    }

    /* Configure the scheduling context to use the first core with budget equal to period */
    err = seL4_SchedControl_Configure(sched_ctrl_start, user_process.sched_context, US_IN_MS, US_IN_MS, 0, 0);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to configure scheduling context");
        return false;
    }

    /* bind sched context, set fault endpoint and priority
     * In MCS, fault end point needed here should be in current thread's cspace.
     * NOTE this will use the unbadged ep unlike above, you might want to mint it with a badge
     * so you can identify which thread faulted in your fault handler */
    err = seL4_TCB_SetSchedParams(user_process.tcb, seL4_CapInitThreadTCB, seL4_MinPrio, APP_PRIORITY,
                                  user_process.sched_context, ep);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to set scheduling params");
        return false;
    }

    /* Provide a name for the thread -- Helpful for debugging */
    NAME_THREAD(user_process.tcb, app_name);

    /* parse the cpio image */
    ZF_LOGI("\nStarting \"%s\"...\n", app_name);
    elf_t elf_file = {};
    unsigned long elf_size;
    size_t cpio_len = _cpio_archive_end - _cpio_archive;
    const char *elf_base = cpio_get_file(_cpio_archive, cpio_len, app_name, &elf_size);
    if (elf_base == NULL) {
        ZF_LOGE("Unable to locate cpio header for %s", app_name);
        return false;
    }
    /* Ensure that the file is an elf file. */
    if (elf_newFile(elf_base, elf_size, &elf_file)) {
        ZF_LOGE("Invalid elf file");
        return false;
    }

    /* set up the stack */
    seL4_Word sp = init_process_stack(&cspace, seL4_CapInitThreadVSpace, &elf_file);

    /* load the elf image from the cpio file */
    err = elf_load(&cspace, &elf_file, &user_process);
    if (err) {
        ZF_LOGE("Failed to load elf image");
        return false;
    }

    /* Start the new process */
    seL4_UserContext context = {
        .pc = elf_getEntryPoint(&elf_file),
        .sp = sp,
    };
    printf("Starting %s at %p\n", APP_NAME, (void *) context.pc);
    err = seL4_TCB_WriteRegisters(user_process.tcb, 1, 0, 2, &context);
    ZF_LOGE_IF(err, "Failed to write registers");
    return err == seL4_NoError;
}

/* Allocate an endpoint and a notification object for sos.
 * Note that these objects will never be freed, so we do not
 * track the allocated ut objects anywhere
 */
static void sos_ipc_init(seL4_CPtr *ipc_ep, seL4_CPtr *ntfn)
{
    /* Create an notification object for interrupts */
    ut_t *ut = alloc_retype(ntfn, seL4_NotificationObject, seL4_NotificationBits);
    ZF_LOGF_IF(!ut, "No memory for notification object");

    /* Bind the notification object to our TCB */
    seL4_Error err = seL4_TCB_BindNotification(seL4_CapInitThreadTCB, *ntfn);
    ZF_LOGF_IFERR(err, "Failed to bind notification object to TCB");

    /* Create an endpoint for user application IPC */
    ut = alloc_retype(ipc_ep, seL4_EndpointObject, seL4_EndpointBits);
    ZF_LOGF_IF(!ut, "No memory for endpoint");
}

/* called by crt */
seL4_CPtr get_seL4_CapInitThreadTCB(void)
{
    return seL4_CapInitThreadTCB;
}

/* tell muslc about our "syscalls", which will be called by muslc on invocations to the c library */
void init_muslc(void)
{
    setbuf(stdout, NULL);

    muslcsys_install_syscall(__NR_set_tid_address, sys_set_tid_address);
    muslcsys_install_syscall(__NR_writev, sys_writev);
    muslcsys_install_syscall(__NR_exit, sys_exit);
    muslcsys_install_syscall(__NR_rt_sigprocmask, sys_rt_sigprocmask);
    muslcsys_install_syscall(__NR_gettid, sys_gettid);
    muslcsys_install_syscall(__NR_getpid, sys_getpid);
    muslcsys_install_syscall(__NR_tgkill, sys_tgkill);
    muslcsys_install_syscall(__NR_tkill, sys_tkill);
    muslcsys_install_syscall(__NR_exit_group, sys_exit_group);
    muslcsys_install_syscall(__NR_ioctl, sys_ioctl);
    muslcsys_install_syscall(__NR_mmap, sys_mmap);
    muslcsys_install_syscall(__NR_brk,  sys_brk);
    muslcsys_install_syscall(__NR_clock_gettime, sys_clock_gettime);
    muslcsys_install_syscall(__NR_nanosleep, sys_nanosleep);
    muslcsys_install_syscall(__NR_getuid, sys_getuid);
    muslcsys_install_syscall(__NR_getgid, sys_getgid);
    muslcsys_install_syscall(__NR_openat, sys_openat);
    muslcsys_install_syscall(__NR_close, sys_close);
    muslcsys_install_syscall(__NR_socket, sys_socket);
    muslcsys_install_syscall(__NR_bind, sys_bind);
    muslcsys_install_syscall(__NR_listen, sys_listen);
    muslcsys_install_syscall(__NR_connect, sys_connect);
    muslcsys_install_syscall(__NR_accept, sys_accept);
    muslcsys_install_syscall(__NR_sendto, sys_sendto);
    muslcsys_install_syscall(__NR_recvfrom, sys_recvfrom);
    muslcsys_install_syscall(__NR_readv, sys_readv);
    muslcsys_install_syscall(__NR_getsockname, sys_getsockname);
    muslcsys_install_syscall(__NR_getpeername, sys_getpeername);
    muslcsys_install_syscall(__NR_fcntl, sys_fcntl);
    muslcsys_install_syscall(__NR_setsockopt, sys_setsockopt);
    muslcsys_install_syscall(__NR_getsockopt, sys_getsockopt);
    muslcsys_install_syscall(__NR_ppoll, sys_ppoll);
    muslcsys_install_syscall(__NR_madvise, sys_madvise);
}

NORETURN void *main_continued(UNUSED void *arg)
{
    /* Initialise other system compenents here */
    seL4_CPtr ipc_ep, ntfn;
    sos_ipc_init(&ipc_ep, &ntfn);
    sos_init_irq_dispatch(
        &cspace,
        seL4_CapIRQControl,
        ntfn,
        IRQ_EP_BADGE,
        IRQ_IDENT_BADGE_BITS
    );

    /* Initialize threads library */
#ifdef CONFIG_SOS_GDB_ENABLED
    /* Create an endpoint that the GDB threads listens to */
    seL4_CPtr gdb_recv_ep;
    ut_t *ep_ut = alloc_retype(&gdb_recv_ep, seL4_EndpointObject, seL4_EndpointBits);
    ZF_LOGF_IF(ep_ut == NULL, "Failed to create GDB endpoint");

    init_threads(ipc_ep, gdb_recv_ep, sched_ctrl_start, sched_ctrl_end);
#else
    init_threads(ipc_ep, ipc_ep, sched_ctrl_start, sched_ctrl_end);
#endif /* CONFIG_SOS_GDB_ENABLED */

    frame_table_init(&cspace, seL4_CapInitThreadVSpace);

    /* run sos initialisation tests */
    run_tests(&cspace);

    /* Map the timer device (NOTE: this is the same mapping you will use for your timer driver -
     * sos uses the watchdog timers on this page to implement reset infrastructure & network ticks,
     * so touching the watchdog timers here is not recommended!) */
    void *timer_vaddr = sos_map_device(&cspace, PAGE_ALIGN_4K(TIMER_MAP_BASE), PAGE_SIZE_4K);

    /* Initialise the network hardware. */
    printf("Network init\n");
    network_init(&cspace, timer_vaddr, ntfn);
    network_console = network_console_init();

    /* Initialize network console buffer */
    SGLIB_QUEUE_INIT(char, nwcs_buf, i, j);
    network_console_register_handler(network_console, write_to_buf);

#ifdef CONFIG_SOS_GDB_ENABLED
    /* Initialize the debugger */
    seL4_Error err = debugger_init(&cspace, seL4_CapIRQControl, gdb_recv_ep);
    ZF_LOGF_IF(err, "Failed to initialize debugger %d", err);
    char secret_string[15] = "Welcome to AOS!";
#endif /* CONFIG_SOS_GDB_ENABLED */

    /* Initialises the timer */
    printf("Timer init\n");
    start_timer(timer_vaddr);
    /* You will need to register an IRQ handler for the timer here.
     * See "irq.h". */

    seL4_Word irq_number = meson_timeout_irq(MESON_TIMER_A);
    bool edge_triggered = true;
    seL4_IRQHandler irq_handler = 0;

    int init_irq_err = sos_register_irq_handler(irq_number, edge_triggered, timer_irq, NULL, &irq_handler);
    ZF_LOGF_IF(init_irq_err != 0, "Failed to initialise IRQ");
    seL4_IRQHandler_Ack(irq_handler);

    /*  Create a thread pool */
    for (size_t i = 0; i < MAX_WORKER_THREADS; ++i) {
        /* Create a notification object */
        ut_t *ut;
        seL4_CPtr thread_ntfn;
        ut = alloc_retype(&thread_ntfn, seL4_NotificationObject, seL4_NotificationBits);
        ZF_LOGF_IF(!ut, "No memory for notification object");
        
        /* Start the worker thread */
        struct syscall_loop_args *worker_sys_loop_args = malloc(sizeof(struct syscall_loop_args));
        sos_thread_t* thread = thread_create(syscall_loop, worker_sys_loop_args, i + 1, false, seL4_MinPrio, thread_ntfn, true);
        
        // worker thread IPC EP is created within 
        worker_sys_loop_args->ep = thread->ipc_ep;
        worker_sys_loop_args->thread_index = i;
        thread_resume(thread);

        worker_threads[i] = thread;
    }


    /* Start user process */
    printf("Start first process\n");
    bool success = start_first_process(APP_NAME, worker_threads[0]->ipc_ep);
    ZF_LOGF_IF(!success, "Failed to start first process");
    

    /* Main thread needs to enter syscall loop as well, for handling interrupts */
    struct syscall_loop_args *main_sys_loop_args = malloc(sizeof(struct syscall_loop_args));
    main_sys_loop_args->ep = ipc_ep;
    syscall_loop(main_sys_loop_args);
}
/*
 * Main entry point - called by crt.
 */
int main(void)
{
    init_muslc();

    /* register the location of the unwind_tables -- this is required for
     * backtrace() to work */
    __register_frame(&__eh_frame_start);

    seL4_BootInfo *boot_info = sel4runtime_bootinfo();

    debug_print_bootinfo(boot_info);

    printf("\nSOS Starting...\n");

    NAME_THREAD(seL4_CapInitThreadTCB, "SOS:root");

    sched_ctrl_start = boot_info->schedcontrol.start;
    sched_ctrl_end = boot_info->schedcontrol.end;

    /* Initialise the cspace manager, ut manager and dma */
    sos_bootstrap(&cspace, boot_info);

    /* switch to the real uart to output (rather than seL4_DebugPutChar, which only works if the
     * kernel is built with support for printing, and is much slower, as each character print
     * goes via the kernel)
     *
     * NOTE we share this uart with the kernel when the kernel is in debug mode. */
    uart_init(&cspace);
    update_vputchar(uart_putchar);

    /* test print */
    printf("SOS Started!\n");

    /* allocate a bigger stack and switch to it -- we'll also have a guard page, which makes it much
     * easier to detect stack overruns */
    seL4_Word vaddr = SOS_STACK;
    for (int i = 0; i < SOS_STACK_PAGES; i++) {
        seL4_CPtr frame_cap;
        ut_t *frame = alloc_retype(&frame_cap, seL4_ARM_SmallPageObject, seL4_PageBits);
        ZF_LOGF_IF(frame == NULL, "Failed to allocate stack page");
        seL4_Error err = map_frame(&cspace, frame_cap, seL4_CapInitThreadVSpace,
                                   vaddr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
        ZF_LOGF_IFERR(err, "Failed to map stack");
        vaddr += PAGE_SIZE_4K;
    }

    utils_run_on_stack((void *) vaddr, main_continued, NULL);

    UNREACHABLE();
}


