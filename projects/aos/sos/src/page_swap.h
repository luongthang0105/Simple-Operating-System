#pragma once
#include "pagetable.h"

extern bool has_init_page_swap;

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
