#include "pagetable.h"
#include "backtrace.h"
static size_t get_pgd_bits(uintptr_t vaddr) {
    return (vaddr >> 39) & 0x1FF;
}
size_t get_pud_bits(uintptr_t vaddr) {
    return (vaddr >> 30) & 0x1FF;
}
size_t get_pd_bits(uintptr_t vaddr) {
    return (vaddr >> 21) & 0x1FF;
}
size_t get_pt_bits(uintptr_t vaddr) {
    return (vaddr >> 12) & 0x1FF;
}

pgd_t *create_pgd() {
    pgd_t *pgd = malloc(sizeof(pgd_t));
    if (!pgd) return NULL;
    for (size_t i = 0; i < TABLE_SIZE_BITS; ++i) {
        pgd->page_upper_directories[i] = NULL;
    }
    return pgd;
}

static pud_t *create_pud() {
    pud_t *pud = malloc(sizeof(pud_t));
    if (!pud) return NULL;
    
    for (size_t i = 0; i < TABLE_SIZE_BITS; ++i) {
        pud->page_directories[i] = NULL;
    }
    return pud;
}

static pd_t *create_pd() {
    pd_t *pd = malloc(sizeof(pd_t));
    if (!pd) return NULL;
    
    for (size_t i = 0; i < TABLE_SIZE_BITS; ++i) {
        pd->page_tables[i] = NULL;
    }
    return pd;
}

static pt_t *create_pt() {
    pt_t *pt = malloc(sizeof(pt_t));
    if (!pt) return NULL;
    for (size_t i = 0; i < TABLE_SIZE_BITS; ++i) {
        pt->frame_metadatas[i] = NULL;
    }
    return pt;
}

int sos_shadow_map_frame(uintptr_t vaddr, frame_metadata_t *frame_metadata, pgd_t *pgd) {
    size_t pgd_index = get_pgd_bits(vaddr);
    size_t pud_index = get_pud_bits(vaddr);
    size_t pd_index = get_pd_bits(vaddr);
    size_t pt_index = get_pt_bits(vaddr);

    pud_t* pud;
    pd_t* pd;
    pt_t* pt;

    if (pgd->page_upper_directories[pgd_index] == NULL) {
        pud = create_pud();
        pgd->page_upper_directories[pgd_index] = pud;
        if (!pud) return -1;
    } else pud = pgd->page_upper_directories[pgd_index];

    if (pud->page_directories[pud_index] == NULL) {
        pd = create_pd();
        pud->page_directories[pud_index] = pd;
        if (!pd) return -1;
    } else pd = pud->page_directories[pud_index];
    
    if (pd->page_tables[pd_index] == NULL) {
        pt = create_pt();
        pd->page_tables[pd_index] = pt;
        if (!pt) return -1;
    } else pt = pd->page_tables[pd_index];

    pt->frame_metadatas[pt_index] = frame_metadata;
    return 0;
}

int sos_shadow_unmap_frame(uintptr_t vaddr, pgd_t *pgd, cspace_t *cspace) {
    size_t pgd_index = get_pgd_bits(vaddr);
    size_t pud_index = get_pud_bits(vaddr);
    size_t pd_index = get_pd_bits(vaddr);
    size_t pt_index = get_pt_bits(vaddr);

    pud_t* pud = pgd->page_upper_directories[pgd_index];
    if (!pud) return -1;

    pd_t* pd = pud->page_directories[pud_index];
    if (!pd) return -1;

    pt_t* pt = pd->page_tables[pd_index];
    if (!pt) return -1;

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
    if (!pud) return NULL;

    pd_t* pd = pud->page_directories[pud_index];
    if (!pd) return NULL;

    pt_t* pt = pd->page_tables[pd_index];
    if (!pt) return NULL;

    return pt->frame_metadatas[pt_index];
}