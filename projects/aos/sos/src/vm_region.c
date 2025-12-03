#include "vm_region.h"
#include <stdbool.h>
#include "syscall_handler/sys_mmap.h"
#include <sys/mman.h>
#include "user_process.h"
#include "utils/util.h"

int init_vm_regions(list_t **vm_regions) {
    *vm_regions = malloc(sizeof(list_t));
    if (!*vm_regions) {
        ZF_LOGE("Failed to alloc vm regions");
        return -1;
    }
    list_init(*vm_regions);
    return 0;
}

void destroy_vm_regions(list_t *vm_regions) {
    if (vm_regions == NULL) return;

    for (struct list_node *cur = vm_regions->head; cur != NULL;) {
        free(cur->data);
        
        struct list_node *next =cur->next;
        free(cur);
        cur = next;
    }
    free(vm_regions);
}

vm_region_t *add_vm_region(list_t *vm_regions, uintptr_t vaddr_base, size_t size, seL4_CapRights_t rights, bool grows_downward) {
    vm_region_t *region = malloc(sizeof(vm_region_t));
    if (region == NULL) {
        // allocation failed
        return NULL;
    }
    region->vaddr_base = vaddr_base;
    region->size = size;
    region->rights = rights;
    region->grows_downward = grows_downward;
    list_prepend(vm_regions, region);
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
bool is_valid_fsr(seL4_Uint64 fsr, vm_region_t *vm_region) {
    return (is_write(fsr) && has_write_perm(vm_region->rights)) || 
           (is_read(fsr) && has_read_perm(vm_region->rights));
}

vm_region_t* find_valid_region(uintptr_t faultaddr, seL4_Uint64 fsr, list_t *vm_regions) {
    if (!vm_regions) return NULL;
    for (struct list_node *cur = vm_regions->head; cur != NULL; cur = cur->next ) {
        vm_region_t *vm_region = (vm_region_t *)cur->data;
        uintptr_t region_start = vm_region->vaddr_base;
        uintptr_t region_end;
        
        if (vm_region->grows_downward) {
            region_end = region_start - vm_region->size;
            if (is_in_range(region_end, region_start, faultaddr) && is_valid_fsr(fsr, vm_region)) {
                return vm_region;
            }
        } else {
            region_end = region_start + vm_region->size;
            if (is_in_range(region_start, region_end, faultaddr) && is_valid_fsr(fsr, vm_region)) {
                return vm_region;
            }
        }
    }
    return NULL;
}

mmap_tree *find_valid_mmap_region(uintptr_t faultaddr, UNUSED seL4_Uint64 fsr, mmap_tree *mmap_tree) { // currently ignore fsr, as we assume mmap is called by malloc with READ/WRITE perms.
    if (!mmap_tree) return NULL;

    struct sglib_mmap_tree_iterator it;
    struct mmap_tree *mmap_node;

    for (mmap_node = sglib_mmap_tree_it_init_inorder(&it, mmap_tree); mmap_node != NULL; mmap_node = sglib_mmap_tree_it_next(&it)) {
        uintptr_t region_start = mmap_node->vaddr_base;
        uintptr_t region_end = region_start + mmap_node->length;
        if (is_in_range(region_start, region_end, faultaddr)) return mmap_node;
    }

    return NULL;
}

seL4_CapRights_t get_mmap_region_rights(mmap_tree* mmap_region) {
    bool canRead = false;
    bool canWrite = false;
    if (mmap_region->prot & PROT_READ) {
        canRead = true;
    }

    if (mmap_region->prot & PROT_WRITE) {
        canWrite = true;
    }
    return seL4_CapRights_new(false, false, canRead, canWrite);
}