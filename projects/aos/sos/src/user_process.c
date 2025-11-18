#include "user_process.h"
#include "pagetable.h"

extern cspace_t cspace;
user_process_t user_process;

int copy_from_user(void* to, const void* from, size_t nbyte) {
    size_t rem_bytes = nbyte;
    size_t bytes_copied = 0;
    uintptr_t from_vaddr = (uintptr_t) from;
    
    char *temp = (char*) to;
    while (rem_bytes > 0) {
        page_metadata_t *page = find_page(from_vaddr, user_process.page_global_directory);
        if (!page) {
            ZF_LOGE("Unable to find a page for buf_vaddr at %p", (void *)from_vaddr);
            return -1;
        }

        // source data of the "from" buf
        unsigned char* source_data = frame_data(page->frame_ref);

        size_t offset = from_vaddr % PAGE_SIZE_4K;
        size_t max_bytes_to_copy = PAGE_SIZE_4K - offset;
        size_t bytes_to_copy = MIN(rem_bytes, max_bytes_to_copy);
        
        memcpy(&temp[bytes_copied], &source_data[offset], bytes_to_copy);

        rem_bytes -= bytes_to_copy;
        from_vaddr += bytes_to_copy;
        bytes_copied += bytes_to_copy;
    }
    return 0;
}

int copy_to_user(void* to, const void* from, size_t nbyte) {
    size_t rem_bytes = nbyte;
    size_t bytes_copied = 0;
    uintptr_t to_vaddr = (uintptr_t) to;
    while (rem_bytes > 0) {
        page_metadata_t *page = find_page(to_vaddr, user_process.page_global_directory);
        if (!page) {
            ZF_LOGI("vaddr %p is not mapped to any frame, trying to allocate frame...", (void*)to_vaddr);
            vm_region_t *valid_region = find_valid_region(to_vaddr, BIT(6), user_process.vm_regions);

            if (valid_region == NULL) {
                ZF_LOGE("vaddr %p resolves to an invalid region access", (void*)to_vaddr);
                return -1;
            }
            
            int result = alloc_map_frame(&cspace, to_vaddr, &user_process, valid_region->rights);
            if (result != 0) {
                ZF_LOGE("Unable to allocate a new frame at %p!\n", (void*)to_vaddr);
                return -1;
            }

            ZF_LOGI("Successfully allocate a new frame at %p", (void*)to_vaddr);
            continue;
        }

        // source data of the "to" buf
        unsigned char* source_data = frame_data(page->frame_ref);

        size_t offset = to_vaddr % PAGE_SIZE_4K;
        size_t max_bytes_to_copy = PAGE_SIZE_4K - offset;
        size_t bytes_to_copy = MIN(rem_bytes, max_bytes_to_copy);
        
        // char *temp = (char*) from;
        memcpy(source_data + offset, from + bytes_copied, bytes_to_copy);

        rem_bytes -= bytes_to_copy;
        to_vaddr += bytes_to_copy;
        bytes_copied += bytes_to_copy;
    }
    return 0;
}