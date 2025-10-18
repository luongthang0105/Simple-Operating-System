#include "pagetable.h"
#include "backtrace.h"
#include "utils.h"

#define PAGE_TABLE_NAME "Page Table"
#define PAGE_DIRECTORY_NAME "Page Directory"
#define PAGE_UPPER_DIRECTORY_NAME "Page Upper Directory"

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
static int map_page_object(
    seL4_CPtr* slot, 
    ut_t* ut,
    seL4_Word vaddr, cspace_t *cspace, user_process_t *user_process,
    const char* page_object_name
) {
    *slot = cspace_alloc_slot(cspace);
    if (*slot == seL4_CapNull) {
        ZF_LOGE("No cptr to alloc paging structure");
        return -1;
    }

    ut = alloc_retype(slot, seL4_ARM_PageTableObject, seL4_PageBits);
    if (ut == NULL) {
        ZF_LOGE("Out of 4k untyped");
        cspace_delete(cspace, *slot);
        cspace_free_slot(cspace, *slot);
        return -1;
    }

    seL4_Error err = seL4_ARM_PageTable_Map(*slot, user_process->vspace, vaddr, seL4_ARM_Default_VMAttributes);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to map %s to Page Global Directory", page_object_name);
        ut_free(ut);

        cspace_delete(cspace, *slot);
        cspace_free_slot(cspace, *slot);
        return -1;
    }

    return 0;
}

static pud_t *create_pud(seL4_Word vaddr, cspace_t *cspace, user_process_t *user_process) {
    pud_t* pud = malloc(sizeof(pud_t));
    if (!pud) {
        ZF_LOGE("Not enough memory to alloc a Page Upper Directory");
        return NULL;
    }
    
    for (size_t i = 0; i < TABLE_SIZE_BITS; ++i) {
        pud->page_directories[i] = NULL;
    }

    int err = map_page_object(&pud->slot, pud->ut, vaddr, cspace, user_process, PAGE_UPPER_DIRECTORY_NAME);
    if (err == -1) {
        free(pud);
        return NULL;
    }

    return pud;
}

static pd_t *create_pd(seL4_Word vaddr, cspace_t *cspace, user_process_t *user_process) {
    pd_t *pd = malloc(sizeof(pd_t));
    if (!pd) {
        ZF_LOGE("Not enough memory to alloc a Page Directory");
        return NULL;
    }
    
    for (size_t i = 0; i < TABLE_SIZE_BITS; ++i) {
        pd->page_tables[i] = NULL;
    }

    int err = map_page_object(&pd->slot, pd->ut, vaddr, cspace, user_process, PAGE_DIRECTORY_NAME);
    if (err == -1) {
        free(pd);
        return NULL;
    }

    return pd;
}

static pt_t *create_pt(seL4_Word vaddr, cspace_t *cspace, user_process_t *user_process) {
    pt_t *pt = malloc(sizeof(pt_t));
    if (!pt) {
        ZF_LOGE("Not enough memory to alloc a Page Table");
        return NULL;
    }
    for (size_t i = 0; i < TABLE_SIZE_BITS; ++i) {
        pt->frame_metadatas[i] = NULL;
    }

    int err = map_page_object(&pt->slot, pt->ut, vaddr, cspace, user_process, PAGE_TABLE_NAME);
    if (err == -1) {
        free(pt);
        return NULL;
    }

    return pt;
}

int sos_shadow_map_frame(   
    uintptr_t vaddr, 
    frame_metadata_t *frame_metadata, 
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

    if (pgd->page_upper_directories[pgd_index] == NULL) {
        pgd->page_upper_directories[pgd_index] = pud = create_pud(vaddr, cspace, user_process);
        if (!pud) {
            ZF_LOGE("Failed to create a %s", PAGE_UPPER_DIRECTORY_NAME);
            return -1;
        }
    } else {
        pud = pgd->page_upper_directories[pgd_index];
    }

    if (pud->page_directories[pud_index] == NULL) {
        pud->page_directories[pud_index] = pd = create_pd(vaddr, cspace, user_process);
        if (!pd) {
            ZF_LOGE("Failed to create a %s", PAGE_DIRECTORY_NAME);
            return -1;
        }
    } else {
        pd = pud->page_directories[pud_index];
    }
    
    if (pd->page_tables[pd_index] == NULL) {
        pd->page_tables[pd_index] = pt = create_pt(vaddr, cspace, user_process);
        if (!pt) {
            ZF_LOGE("Failed to create a %s", PAGE_TABLE_NAME);
            return -1;
        }
    } else {
        pt = pd->page_tables[pd_index];
    }

    seL4_Error err = seL4_ARM_Page_Map(frame_metadata->frame_cap, user_process->vspace, vaddr, rights, attr);
    pt->frame_metadatas[pt_index] = frame_metadata;
    return 0;
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

    frame_metadata_t *frame;
    frame = pt->frame_metadatas[pt_index];
    pt->frame_metadatas[pt_index] = NULL;

    if (!frame) {
        ZF_LOGE("Unable to find the mapped page at vaddr=%p", vaddr);
        return -1;
    }

    seL4_Error err = seL4_ARM_Page_Unmap(frame->frame_cap);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to unmap the page when deallocating the frames");
        return -1;
    }

    err = cspace_delete(cspace, frame->frame_cap);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to delete the copy of the frame cap");
        return -1;
    }
    cspace_free_slot(cspace, frame->frame_cap);
    free_frame(frame->frame_ref);

    return 0;
}

frame_metadata_t *find_frame(uintptr_t vaddr, pgd_t *pgd) {
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

    return pt->frame_metadatas[pt_index];
}