
#include "../user_process.h"
#include "../pagetable.h"

extern cspace_t cspace;
int handle_sos_munmap() {
    ZF_LOGV("syscall:munmap!\n");

    uintptr_t addr = seL4_GetMR(1);
    size_t length = seL4_GetMR(2);

    // addr must be page aligned
    if (addr & (PAGE_SIZE - 1)) {
        return -1;
    }

    user_process_t *user_process = get_current_user_process();

    struct mmap_tree e;
    e.vaddr_base = (uintptr_t)addr;

    struct mmap_tree *member = sglib_mmap_tree_find_member(user_process->mmap_region, &e);
    if (member == NULL) return -1;

    size_t num_remaining_pages_to_dealloc = length;
    uintptr_t next_vaddr_to_dealloc = addr;

    while (num_remaining_pages_to_dealloc > 0) {
        page_metadata_t **page_ptr = find_page_ptr(next_vaddr_to_dealloc, user_process->page_global_directory);
        if (page_ptr == NULL || *page_ptr == NULL) break;
        destroy_page(*page_ptr, &cspace, user_process->vspace);
        *page_ptr = NULL; // sets this to NULL, so destroy_pt will not call destroy_page on this

        num_remaining_pages_to_dealloc -= PAGE_SIZE_4K;
        user_process->size -= 1;

        next_vaddr_to_dealloc += PAGE_SIZE_4K;
    }

    // remove the mmap region out of the mmap tree
    sglib_mmap_tree_delete(&user_process->mmap_region, member);
    return 0;
}