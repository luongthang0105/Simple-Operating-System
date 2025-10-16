#pragma once

#define TABLE_SIZE_BITS (1<<9)
#include <cspace/cspace.h>
#include "frame_table.h"

struct frame_metadata
{
    frame_ref_t frame_ref;
    seL4_Word vaddr;
    seL4_CPtr frame_cap;
};
typedef struct frame_metadata frame_metadata_t;

struct page_table {
    frame_metadata_t *frame_metadatas[TABLE_SIZE_BITS];
};
typedef struct page_table pt_t;

struct page_directory {
    pt_t *page_tables[TABLE_SIZE_BITS];
};
typedef struct page_directory pd_t;

struct page_upper_directory {
    pd_t *page_directories[TABLE_SIZE_BITS];
};
typedef struct page_upper_directory pud_t;

struct page_global_directory {
    pud_t *page_upper_directories[TABLE_SIZE_BITS];
};
typedef struct page_global_directory pgd_t;

/* Returns 0 on success. If malloc fails (due to out of memory), returns -1. */
int sos_shadow_map_frame(uintptr_t vaddr, frame_metadata_t *frame_metadata, pgd_t *pgd);

/* Returns 0 on success. Returns -1 if failed. */
int sos_shadow_unmap_frame(uintptr_t vaddr, pgd_t *pgd, cspace_t *cspace);

pgd_t *create_pgd();
frame_metadata_t *find_frame(uintptr_t vaddr, pgd_t *pgd);

