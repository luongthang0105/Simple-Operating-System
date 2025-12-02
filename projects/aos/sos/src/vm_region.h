#pragma once
#include <cspace/cspace.h>
#include "syscall_handler/sys_mmap.h"

struct vm_region
{
    uintptr_t vaddr_base;
    size_t size;
    seL4_CapRights_t rights;
    bool grows_downward;
};
typedef struct vm_region vm_region_t;

int init_vm_regions(list_t **vm_regions);
void destroy_vm_regions(list_t *vm_regions);
vm_region_t *add_vm_region(list_t *vm_regions, uintptr_t vaddr_base, size_t size, seL4_CapRights_t rights, bool grows_downward);
vm_region_t* find_valid_region(uintptr_t faultaddr, seL4_Uint64 fsr, list_t *vm_regions);
mmap_tree *find_valid_mmap_region(uintptr_t faultaddr, seL4_Uint64 fsr, mmap_tree *mmap_tree);