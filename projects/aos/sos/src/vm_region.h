#pragma once
#include <cspace/cspace.h>

struct vm_region
{
    uintptr_t vaddr_base;
    size_t size;
    seL4_CapRights_t permission;
    bool grows_downward;
};
typedef struct vm_region vm_region_t;

vm_region_t *add_vm_region(list_t *vm_regions, uintptr_t vaddr_base, size_t size, seL4_CapRights_t permission, bool grows_downward);
