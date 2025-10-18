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

bool is_in_range(uintptr_t start, uintptr_t end, uintptr_t addr) {
    return start <= addr && addr < end;
}

bool is_write(seL4_Uint64 fsr) { return (fsr & (BIT(6))) != 0; };
bool is_read(seL4_Uint64 fsr) { return (fsr & (BIT(6))) == 0; };
bool has_write_perm(seL4_CapRights_t permission) {
    return permission.words[0] & seL4_CanWrite.words[0];
}
bool has_read_perm(seL4_CapRights_t permission) {
    return permission.words[0] & seL4_CanRead.words[0];
}
vm_region_t* find_valid_region(uintptr_t faultaddr, seL4_Uint64 fsr, list_t *vm_regions) {
    for (struct list_node *cur = vm_regions->head; cur != NULL; cur = cur->next ) {
        vm_region_t *vm_region = (vm_region_t *)cur->data;
        uintptr_t region_start = vm_region->vaddr_base;
        uintptr_t region_end;
        if (vm_region->grows_downward) {
            region_end = region_start - vm_region->size;
            if (is_in_range(region_end, region_start, faultaddr) &&
                (
                    (is_write(fsr) && has_write_perm(vm_region->permission)) || 
                    (is_read(fsr) && has_read_perm(vm_region->permission)
                ))
            ) return vm_region;
        } else {
            region_end = region_start + vm_region->size;
            if (is_in_range(region_start, region_end, faultaddr)) return vm_region;
        }
    }
    return NULL;
}