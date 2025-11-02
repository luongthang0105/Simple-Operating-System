#include <sos/gen_config.h>
#include "page_swap.h"
#include "mapping.h"
#ifdef CONFIG_SOS_FRAME_LIMIT
#define PAGES_QUEUE_MAX_SIZE ((CONFIG_SOS_FRAME_LIMIT == 0ul) ? (1 << 19) : CONFIG_SOS_FRAME_LIMIT)
#else
#define PAGES_QUEUE_MAX_SIZE (1 << 19)
#endif

extern cspace_t cspace;

typedef struct pages_queue
{
    page_metadata_t *arr[PAGES_QUEUE_MAX_SIZE];
    size_t i;
    size_t j;
} pages_queue_t;

pages_queue_t in_memory_pages;

SGLIB_DEFINE_QUEUE_FUNCTIONS(pages_queue_t, page_metadata_t *, arr, i, j, PAGES_QUEUE_MAX_SIZE)

int swap_to_mem(page_metadata_t *page) {
    // read the content of this page from the disk

    // get the freed frame
    
    // write the content of this page to the frame
    // write_page_to_disk(page);

    // update reference bit and offset
    page->reference_bit = 1;
    page->pagefile_offset = -1;
    sglib_pages_queue_t_add(&in_memory_pages, page);
    
    return 0;
}

frame_t *evict_page() {
    while (!sglib_pages_queue_t_is_empty(&in_memory_pages)) {
        page_metadata_t *page = sglib_pages_queue_t_first_element(&in_memory_pages);
        sglib_pages_queue_t_delete_first(&in_memory_pages);

        if (page->reference_bit == 1) { /* give it a second chance */
            sglib_pages_queue_t_add(&in_memory_pages, page);
            page->reference_bit = 0;

            // unmap the page so that we can simulate the reference bit via vm fault
            seL4_Error err = seL4_ARM_Page_Unmap(page->frame_cap);
            if (err != seL4_NoError) {
                ZF_LOGE("Unable to unmap the page, seL4_Error = %d\n", err);
                return NULL;
            }
        } else if (page->reference_bit == 0) {
            // write_page_to_disk(page);

            // unmap the page, delete its frame cap and slot
            seL4_Error err = dealloc_unmap_frame(&cspace, page);
            if (err != seL4_NoError) {
                ZF_LOGE("Unable to deallocate and unmap the page, seL4_Error = %d\n", err);
                return NULL;
            }

            return frame_from_ref(page->frame_ref);
        }
    }

    return NULL;
}

seL4_Error reference_page(page_metadata_t *page, seL4_CPtr vspace, seL4_Word vaddr, seL4_CapRights_t rights) {
    page->reference_bit = 1;
    seL4_Error err = seL4_ARM_Page_Map(page->frame_cap, vspace, vaddr, rights, seL4_ARM_Default_VMAttributes);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to perform page map. seL4_Error = %d", err);
        return CSPACE_ERROR;
    }
    return seL4_NoError;
}