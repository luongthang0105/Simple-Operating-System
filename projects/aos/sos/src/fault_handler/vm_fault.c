#include "vm_fault.h"
#include "../user_process.h"
#include "../pagetable.h"
#include "../syscall_handler/sys_mmap.h"
#include "../mapping.h"

extern cspace_t cspace;

int handle_vm_fault(seL4_Fault_t fault) {
    uintptr_t original_faultadrr = seL4_Fault_VMFault_get_Addr(fault);

    seL4_Uint64 fsr = seL4_Fault_VMFault_get_FSR(fault);

    user_process_t *user_process = get_current_user_process();

    // find the vm_region or mmap_region that this faultaddr lies within
    vm_region_t *valid_region = find_valid_region(original_faultadrr, fsr, user_process->vm_regions);
    mmap_tree *valid_mmap_region = NULL;

    if (valid_region == NULL) {
        // ZF_LOGE("Fault address %p resolves to an invalid region access", (void*)original_faultadrr);
        valid_mmap_region = find_valid_mmap_region(original_faultadrr, fsr, user_process->mmap_region);
        if (valid_mmap_region == NULL) {
            ZF_LOGE("Fault address %p resolves to an invalid mmap region access", (void*)original_faultadrr);
            return -1;
        }
    }

    seL4_CapRights_t rights;
    if (valid_region != NULL) {
        rights = valid_region->rights;
    } else if (valid_mmap_region != NULL) {
        rights = get_mmap_region_rights(valid_mmap_region);
    }
   
    uintptr_t faultaddr = PAGE_ALIGN_4K(original_faultadrr);
    // find the associated page of this faultaddr
    page_metadata_t *page = find_page(original_faultadrr, user_process->page_global_directory);
    if (page == NULL) {  /* faultaddr has not been mapped, try alloc a frame and map that frame to faultaddr */
        return alloc_map_frame(&cspace, faultaddr, user_process, rights);
    }

    return 0;
}