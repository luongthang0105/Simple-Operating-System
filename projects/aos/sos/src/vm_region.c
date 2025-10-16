#include "vm_region.h"
#include <stdbool.h>
vm_region_t *add_vm_region(list_t *vm_regions, uintptr_t vaddr_base, size_t size, seL4_CapRights_t permission, bool grows_downward) {
    vm_region_t *region = malloc(sizeof(vm_region_t));
    if (region == NULL) {
        // allocation failed
        return NULL;
    }
    region->vaddr_base = vaddr_base;
    region->size = size;
    region->permission = permission;
    region->grows_downward = grows_downward;
    list_append(vm_regions, region);
    return region;
}
