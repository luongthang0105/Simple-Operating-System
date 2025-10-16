#pragma once
#include "ut.h"
#include "vm_region.h"
#include "pagetable.h"

struct user_process {
    ut_t *tcb_ut;
    seL4_CPtr tcb;
    ut_t *vspace_ut;
    seL4_CPtr vspace;

    ut_t *ipc_buffer_ut;
    seL4_CPtr ipc_buffer;

    ut_t *sched_context_ut;
    seL4_CPtr sched_context;

    cspace_t cspace;

    ut_t *stack_ut;
    seL4_CPtr stack;
    uintptr_t guard_page_vaddr;

    list_t *paging_objects;
    pgd_t *page_global_directory;    
    list_t *vm_regions;

    vm_region_t* heap_region;
    vm_region_t* stack_region;

};
typedef struct user_process user_process_t;
