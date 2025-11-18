#include "vm_fault.h"
#include "../user_process.h"
#include "../pagetable.h"

extern cspace_t cspace;

int handle_vm_fault(seL4_Fault_t fault, seL4_CPtr worker_thread_ntfn) {
    uintptr_t original_faultadrr = seL4_Fault_VMFault_get_Addr(fault);
    seL4_Uint64 fsr = seL4_Fault_VMFault_get_FSR(fault);

    // find the vm_region that this faultaddr lies within
    vm_region_t *valid_region = find_valid_region(original_faultadrr, fsr, user_process.vm_regions);
    if (valid_region == NULL) {
        ZF_LOGE("Fault address %p resolves to an invalid region access", (void*)original_faultadrr);
        return -1;
    }
    
    uintptr_t faultaddr = PAGE_ALIGN_4K(original_faultadrr);
    // find the associated page of this faultaddr
    page_metadata_t *page = find_page(original_faultadrr, user_process.page_global_directory);
    if (page == NULL) {  /* faultaddr has not been mapped, try alloc a frame and map that frame to faultaddr */
        return alloc_map_frame(&cspace, faultaddr, &user_process, valid_region->rights);
    }
    return 0;
}