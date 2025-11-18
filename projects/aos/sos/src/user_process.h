#pragma once
#include "ut.h"
#include "vm_region.h"
#include <nfsc/libnfs.h>
#include <sossharedapi/vfs.h>

struct page_global_directory;
typedef struct page_global_directory pgd_t;

struct user_process {
    ut_t *tcb_ut;
    seL4_CPtr tcb;
    ut_t *vspace_ut;
    seL4_CPtr vspace;

    seL4_CPtr ipc_buffer;

    ut_t *sched_context_ut;
    seL4_CPtr sched_context;

    cspace_t cspace;

    ut_t *stack_ut;
    seL4_CPtr stack;
    uintptr_t guard_page_vaddr;

    pgd_t *page_global_directory;    
    list_t *vm_regions;

    vm_region_t* heap_region;
    vm_region_t* stack_region;

    // filesystem
    /* main thread running the callback will assign a (struct nfsdir*) to this variable, 
    so worker thread can use this*/ 
    struct nfsdir* curr_dir; 

    vfs_t* vfs;
};
typedef struct user_process user_process_t;
extern user_process_t user_process;

/** Copy data from SOS to user app.
 *  @returns 0 if `nbyte` was successfully copied, -1 otherwise.
*/
int copy_to_user(void* to, const void* from, size_t nbyte);

/** 
 * Copy data from user app to SOS. 
 * @returns 0 if `nbyte` was successfully copied, -1 otherwise.
*/
int copy_from_user(void* to, const void* from, size_t nbyte);
