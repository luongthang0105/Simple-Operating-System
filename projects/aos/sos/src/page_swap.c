#include <sos/gen_config.h>
#include "page_swap.h"

#ifdef CONFIG_SOS_FRAME_LIMIT
#define PAGES_QUEUE_MAX_SIZE ((CONFIG_SOS_FRAME_LIMIT == 0ul) ? (1 << 19) : CONFIG_SOS_FRAME_LIMIT)
#else
#define PAGES_QUEUE_MAX_SIZE (1 << 19)
#endif


typedef struct pages_queue
{
    page_metadata_t *arr[PAGES_QUEUE_MAX_SIZE];
    size_t i;
    size_t j;
} pages_queue_t;

pages_queue_t pages_queue;

SGLIB_DEFINE_QUEUE_FUNCTIONS(pages_queue_t, page_metadata_t *, arr, i, j, PAGES_QUEUE_MAX_SIZE)

void swap_to_mem(page_metadata_t *page) {
    // read the content of this page from the disk

    // get the freed frame
    
    // write the content of this page to the frame

    // update reference bit and offset
    page->reference_bit = 1;
    page->offset = -1;
    sglib_pages_queue_t_add(&pages_queue, page);
}

frame_t *evict_page() {
    while (!sglib_pages_queue_t_is_empty(&pages_queue)) {
        page_metadata_t *page = sglib_pages_queue_t_first_element(&pages_queue);
        sglib_pages_queue_t_delete_first(&pages_queue);

        if (page->reference_bit == 1) { /* give it a second chance */
            sglib_pages_queue_t_add(&pages_queue, page);
            page->reference_bit = 0;

        } else if (page->reference_bit == 0) {
            seL4_Error err = seL4_ARM_Page_Unmap(page->frame_cap);
            if (err != seL4_NoError) {
                ZF_LOGE("Unable to unmap the page when deallocating the frames");
                return NULL;
            }
            write_page_to_disk(page);
            // TODO: delete slot, delete frame_cap
            return frame_from_ref(page->frame_ref);
        }
    }
}
