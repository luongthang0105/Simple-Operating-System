#include "pagetable.h"
#include "backtrace.h"
#include "utils.h"
#include "user_process.h"
#include "page_swap.h"

#define PAGE_TABLE_NAME "Page Table"
#define PAGE_DIRECTORY_NAME "Page Directory"
#define PAGE_UPPER_DIRECTORY_NAME "Page Upper Directory"

extern user_process_t user_process;

static size_t get_pgd_bits(uintptr_t vaddr) {
    return (vaddr >> 39) & 0x1FF;
}
static size_t get_pud_bits(uintptr_t vaddr) {
    return (vaddr >> 30) & 0x1FF;
}
static size_t get_pd_bits(uintptr_t vaddr) {
    return (vaddr >> 21) & 0x1FF;
}
static size_t get_pt_bits(uintptr_t vaddr) {
    return (vaddr >> 12) & 0x1FF;
}

pgd_t *create_pgd() {
    pgd_t *pgd = malloc(sizeof(pgd_t));
    if (!pgd) {
        ZF_LOGE("Not enough memory to alloc a Page Global Directory");
        return NULL;
    }
    for (size_t i = 0; i < TABLE_SIZE_BITS; ++i) {
        pgd->page_upper_directories[i] = NULL;
    }
    return pgd;
}
/*  Maps a page object (Page Upper Directory, Page Directory, or Page Table) into the root servers page global directory.
    Returns 0 on success. On failure, returns -1 and caller must free the page object to prevent memory leak.    
*/
static seL4_Error map_page_object(
    seL4_CPtr* slot, 
    ut_t* ut,
    seL4_Word vaddr, cspace_t *cspace, user_process_t *user_process,
    const char* page_object_name
) {
    *slot = cspace_alloc_slot(cspace);
    if (*slot == seL4_CapNull) {
        ZF_LOGE("No cptr to alloc paging structure");
        return seL4_NotEnoughMemory;
    }

    ut = alloc_retype(slot, seL4_ARM_PageTableObject, seL4_PageBits);
    if (ut == NULL) {
        ZF_LOGE("Out of 4k untyped");
        cspace_delete(cspace, *slot);
        cspace_free_slot(cspace, *slot);
        return seL4_NotEnoughMemory;
    }

    seL4_Error err = seL4_ARM_PageTable_Map(*slot, user_process->vspace, vaddr, seL4_ARM_Default_VMAttributes);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to map %s to Page Global Directory", page_object_name);
        ut_free(ut);

        cspace_delete(cspace, *slot);
        cspace_free_slot(cspace, *slot);
        return err;
    }

    return seL4_NoError;
}

static seL4_Error create_pud(pud_t** source_pud, seL4_Word vaddr, cspace_t *cspace, user_process_t *user_process) {
    pud_t* pud = malloc(sizeof(pud_t));
    if (!pud) {
        ZF_LOGE("Not enough memory to alloc a Page Upper Directory");
        return seL4_NotEnoughMemory;
    }
    
    for (size_t i = 0; i < TABLE_SIZE_BITS; ++i) {
        pud->page_directories[i] = NULL;
    }

    seL4_Error err = map_page_object(&pud->slot, pud->ut, vaddr, cspace, user_process, PAGE_UPPER_DIRECTORY_NAME);
    if (err != seL4_NoError) {
        free(pud);
        return err;
    }

    *source_pud = pud;
    return seL4_NoError;
}

static seL4_Error create_pd(pd_t** source_pd, seL4_Word vaddr, cspace_t *cspace, user_process_t *user_process) {
    pd_t* pd = malloc(sizeof(pd_t));
    if (!pd) {
        ZF_LOGE("Not enough memory to alloc a Page Directory");
        return seL4_NotEnoughMemory;
    }
    
    for (size_t i = 0; i < TABLE_SIZE_BITS; ++i) {
        pd->page_tables[i] = NULL;
    }

    seL4_Error err = map_page_object(&pd->slot, pd->ut, vaddr, cspace, user_process, PAGE_DIRECTORY_NAME);
    if (err != seL4_NoError) {
        free(pd);
        return err;
    }

    *source_pd = pd;
    return seL4_NoError;
}

static seL4_Error create_pt(pt_t** source_pt, seL4_Word vaddr, cspace_t *cspace, user_process_t *user_process) {
    pt_t *pt = malloc(sizeof(pt_t));
    if (!pt) {
        ZF_LOGE("Not enough memory to alloc a Page Table");
        return seL4_NotEnoughMemory;
    }
    for (size_t i = 0; i < TABLE_SIZE_BITS; ++i) {
        pt->page_metadatas[i] = NULL;
    }

    seL4_Error err = map_page_object(&pt->slot, pt->ut, vaddr, cspace, user_process, PAGE_TABLE_NAME);
    if (err != seL4_NoError) {
        free(pt);
        return err;
    }

    *source_pt = pt;
    return seL4_NoError;
}

seL4_Error sos_shadow_map_frame(   
    uintptr_t vaddr, 
    page_metadata_t *page_metadata, 
    cspace_t *cspace,
    user_process_t *user_process,
    seL4_CapRights_t rights, 
    seL4_ARM_VMAttributes attr
) {
    size_t pgd_index = get_pgd_bits(vaddr);
    size_t pud_index = get_pud_bits(vaddr);
    size_t pd_index = get_pd_bits(vaddr);
    size_t pt_index = get_pt_bits(vaddr);

    pgd_t* pgd = user_process->page_global_directory;
    pud_t* pud;
    pd_t* pd;
    pt_t* pt;

    seL4_Error err;
    if (pgd->page_upper_directories[pgd_index] == NULL) {
        err = create_pud(&pgd->page_upper_directories[pgd_index], vaddr, cspace, user_process);
        if (err != seL4_NoError) {
            ZF_LOGE("Failed to create a %s, seL4_Error: %d", PAGE_UPPER_DIRECTORY_NAME, err);
            return err;
        }
    }
    pud = pgd->page_upper_directories[pgd_index];

    if (pud->page_directories[pud_index] == NULL) {
        err = create_pd(&pud->page_directories[pud_index], vaddr, cspace, user_process);
        if (err != seL4_NoError) {
            ZF_LOGE("Failed to create a %s, seL4_Error: %d", PAGE_DIRECTORY_NAME, err);
            return err;
        }
    } 
    pd = pud->page_directories[pud_index];

    
    if (pd->page_tables[pd_index] == NULL) {
        err = create_pt(&pd->page_tables[pd_index], vaddr, cspace, user_process);
        if (err != seL4_NoError) {
            ZF_LOGE("Failed to create a %s, seL4_Error: %d", PAGE_TABLE_NAME, err);
            return err;
        }
    }
    pt = pd->page_tables[pd_index];

    err = seL4_ARM_Page_Map(page_metadata->frame_cap, user_process->vspace, vaddr, rights, attr);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to perform page map. seL4_Error = %d", err);
        return err;
    }

    pt->page_metadatas[pt_index] = page_metadata;
    return seL4_NoError;
}

int sos_shadow_unmap_frame(uintptr_t vaddr, pgd_t *pgd, cspace_t *cspace) {
    size_t pgd_index = get_pgd_bits(vaddr);
    size_t pud_index = get_pud_bits(vaddr);
    size_t pd_index = get_pd_bits(vaddr);
    size_t pt_index = get_pt_bits(vaddr);

    pud_t* pud = pgd->page_upper_directories[pgd_index];
    if (!pud) {
        ZF_LOGE("%s does not exist", PAGE_UPPER_DIRECTORY_NAME);
        return -1;
    }

    pd_t* pd = pud->page_directories[pud_index];
    if (!pd) {
        ZF_LOGE("%s does not exist", PAGE_DIRECTORY_NAME);
        return -1;
    }

    pt_t* pt = pd->page_tables[pd_index];
    if (!pt) {
        ZF_LOGE("%s does not exist", PAGE_TABLE_NAME);
        return -1;
    }

    page_metadata_t *page;
    page = pt->page_metadatas[pt_index];
    pt->page_metadatas[pt_index] = NULL;

    if (!page) {
        ZF_LOGE("Unable to find the mapped page at vaddr=%p", (void*)vaddr);
        return -1;
    }

    seL4_Error err = seL4_ARM_Page_Unmap(page->frame_cap);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to unmap the page when deallocating the frames");
        return -1;
    }

    err = cspace_delete(cspace, page->frame_cap);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to delete the copy of the frame cap");
        return -1;
    }
    cspace_free_slot(cspace, page->frame_cap);
    free_frame(page->frame_ref);

    return 0;
}

page_metadata_t *find_page(uintptr_t vaddr, pgd_t *pgd) {
    size_t pgd_index = get_pgd_bits(vaddr);
    size_t pud_index = get_pud_bits(vaddr);
    size_t pd_index = get_pd_bits(vaddr);
    size_t pt_index = get_pt_bits(vaddr);

    pud_t* pud = pgd->page_upper_directories[pgd_index];
    if (!pud) {
        ZF_LOGE("%s does not exist", PAGE_UPPER_DIRECTORY_NAME);
        return NULL;
    }

    pd_t* pd = pud->page_directories[pud_index];
    if (!pd) {
        ZF_LOGE("%s does not exist", PAGE_DIRECTORY_NAME);
        return NULL;
    }

    pt_t* pt = pd->page_tables[pd_index];
    if (!pt) {
        ZF_LOGE("%s does not exist", PAGE_TABLE_NAME);
        return NULL;
    }
    int ret = 0;
    page_metadata_t *page = pt->page_metadatas[pt_index];
    if (page != NULL) { /* page is either on disk or in memory */
        if (page->pagefile_offset != -1) {   /* page is on disk */
            ret = swap_to_mem(page);
        } else {                             /* page is still in memory */
            // printf("page is still in memory with reference bit = %d!!\n", page->reference_bit);
            ret = reference_page(page, user_process.vspace, vaddr, page->rights);
        }
    }
    
    return (ret == 0) ? page : NULL;
}

unsigned char* find_frame_data(uintptr_t vaddr, pgd_t *pgd) {
    // find the page associated with this buf_vaddr
    page_metadata_t *page = find_page(vaddr, pgd);
    if (!page) {
        ZF_LOGE("page not found for vaddr=%p\n", (void*)vaddr);
        return NULL;
    }
    return frame_data(page->frame_ref);
}