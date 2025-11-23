#include <sel4/shared_types_gen.h>
#include "../user_process.h"

extern cspace_t cspace;

uintptr_t handle_sos_brk() {
    ZF_LOGV("syscall: brk!\n");

    user_process_t *user_process = get_current_user_process();

    uintptr_t new_brk = seL4_GetMR(1);
    if (new_brk == 0) {
        return user_process->heap_region->vaddr_base;
    }

    /* check if the new_brk is valid: 
        - within the heap & stack bottom
    */
    
    uintptr_t guard_page_vaddr = user_process->guard_page_vaddr;
    uintptr_t heap_base = user_process->heap_region->vaddr_base;
    uintptr_t curr_brk = heap_base + user_process->heap_region->size;

    if (new_brk < heap_base || new_brk > guard_page_vaddr) {
        ZF_LOGE("New program break is not valid");
        return 0;
    }

    if (curr_brk == new_brk) {
        return new_brk;
    } else if (curr_brk < new_brk) { /* allocate more memory */
        uintptr_t next_page_vaddr_to_alloc = ROUND_UP(curr_brk, PAGE_SIZE_4K);
    
        while (next_page_vaddr_to_alloc < new_brk) {
            int result = alloc_map_frame(&cspace, next_page_vaddr_to_alloc, user_process, user_process->heap_region->rights);
            if (result != 0) {
                ZF_LOGE("Unable to allocate a new frame at %p!\n", (void*)next_page_vaddr_to_alloc);
                return 0;
            }
            next_page_vaddr_to_alloc += PAGE_SIZE_4K;
        }
    } else { /* deallocate frames */
        uintptr_t next_page_vaddr_to_dealloc = ROUND_DOWN(curr_brk, PAGE_SIZE_4K);
        
        while (next_page_vaddr_to_dealloc >= new_brk) {
            int ret = sos_shadow_unmap_frame(next_page_vaddr_to_dealloc, user_process->page_global_directory, &cspace);
            if (ret == -1) {
                return 0;
            }
            next_page_vaddr_to_dealloc -= PAGE_SIZE_4K;
        }
    }

    user_process->heap_region->size = new_brk - heap_base;
    return new_brk;
}