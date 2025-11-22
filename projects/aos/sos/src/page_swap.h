#pragma once
#include "pagetable.h"
#ifdef CONFIG_SOS_FRAME_LIMIT
#define PAGES_QUEUE_MAX_SIZE    ((CONFIG_SOS_FRAME_LIMIT == 0ul) ? (1 << 19) : CONFIG_SOS_FRAME_LIMIT)
#define OFFSET_QUEUE_MAX_SIZE   ((CONFIG_SOS_FRAME_LIMIT == 0ul) ? (1 << 19) : (CONFIG_SOS_FRAME_LIMIT * 150))
#else
#define PAGES_QUEUE_MAX_SIZE (1 << 19)
#define OFFSET_QUEUE_MAX_SIZE (1 << 19)
#endif

extern bool has_init_page_swap;
sync_mutex_t *in_memory_pages_mutex;

/*  A queue that contains the in-memory pages that is used to evict a page using the second-chance replacement policy.
*/
typedef struct pages_queue
{
    page_metadata_t *arr[PAGES_QUEUE_MAX_SIZE];
    size_t i;
    size_t j;
} pages_queue_t;

pages_queue_t in_memory_pages;
sync_mutex_t *in_memory_pages_mutex;

/*  A queue that contains the currently free space in the pagefile (determined by the offset)
    It should always have at least one item in it, which is the offset to the end of the file.
    The initial item in the queue is offset 0.
*/
typedef struct free_pagefile_offsets {
    size_t arr[OFFSET_QUEUE_MAX_SIZE];
    size_t i;
    size_t j;
    size_t eof_offset;
} offset_queue_t;

offset_queue_t free_pagefile_offsets;
sync_mutex_t *free_pagefile_offsets_mutex;

/*  If current queue is full, evict a page from its allocated frame using the second-chance replacement policy.
    Otherwise, do nothing. After calling `evict_page()`, it is guaranteed that there is at least one free frame in the frame table.
*/
void evict_page();

/**
 *  Swap a page from the disk to memory.
 *  
 *  This first evicts a page, then reads the content of the pagefile
 *  starting at the given offset, and writes that content to the freed frame
 *
 *  @param page   page that is about to be swapped from disk to memory 
 *  @return 0 on success.
 */
int swap_to_mem(page_metadata_t *page);

void in_memory_pages_add(page_metadata_t *page);

void init_page_swap();
seL4_Error reference_page(page_metadata_t *page, seL4_CPtr vspace, seL4_Word vaddr, seL4_CapRights_t rights);
