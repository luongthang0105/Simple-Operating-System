/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */
#include <sel4/sel4.h>
#include <sel4/sel4_arch/mapping.h>

#include "mapping.h"
#include "vmem_layout.h"
#include <utils/list.h>
#include "page_swap.h"
/**
 * Retypes and maps a page table into the root servers page global directory
 * @param cspace that the cptrs refer to
 * @param vaddr  the virtual address of the mapping
 * @param ut     a 4k untyped object
 * @param empty  an empty slot to retype into a pt
 * @return 0 on success
 */
static seL4_Error retype_map_pt(cspace_t *cspace, seL4_CPtr vspace, seL4_Word vaddr, seL4_CPtr ut, seL4_CPtr empty)
{

    seL4_Error err = cspace_untyped_retype(cspace, ut, empty, seL4_ARM_PageTableObject, seL4_PageBits);
    if (err) {
        return err;
    }

    return seL4_ARM_PageTable_Map(empty, vspace, vaddr, seL4_ARM_Default_VMAttributes);
}

/**
 * Retypes and maps a page directory into the root servers page global directory
 * @param cspace that the cptrs refer to
 * @param vaddr  the virtual address of the mapping
 * @param ut     a 4k untyped object
 * @param empty  an empty slot to retype into a pd
 * @return 0 on success
 */
static seL4_Error retype_map_pd(cspace_t *cspace, seL4_CPtr vspace, seL4_Word vaddr, seL4_CPtr ut, seL4_CPtr empty)
{

    seL4_Error err = cspace_untyped_retype(cspace, ut, empty, seL4_ARM_PageDirectoryObject, seL4_PageBits);
    if (err) {
        return err;
    }

    return seL4_ARM_PageDirectory_Map(empty, vspace, vaddr, seL4_ARM_Default_VMAttributes);
}

/**
 * Retypes and maps a page upper directory into the root servers page global directory
 * @param cspace that the cptrs refer to
 * @param vaddr  the virtual address of the mapping
 * @param ut     a 4k untyped object
 * @param empty  an empty slot to retype into a pud
 * @return 0 on success
 */
static seL4_Error retype_map_pud(cspace_t *cspace, seL4_CPtr vspace, seL4_Word vaddr, seL4_CPtr ut,
                                 seL4_CPtr empty)
{

    seL4_Error err = cspace_untyped_retype(cspace, ut, empty, seL4_ARM_PageUpperDirectoryObject, seL4_PageBits);
    if (err) {
        return err;
    }
    return seL4_ARM_PageUpperDirectory_Map(empty, vspace, vaddr, seL4_ARM_Default_VMAttributes);
}

static seL4_Error map_frame_impl(cspace_t *cspace, seL4_CPtr frame_cap, seL4_CPtr vspace, seL4_Word vaddr,
                                 seL4_CapRights_t rights, seL4_ARM_VMAttributes attr,
                                 seL4_CPtr *free_slots, seL4_Word *used)
{
    /* Attempt the mapping */
    seL4_Error err = seL4_ARM_Page_Map(frame_cap, vspace, vaddr, rights, attr);
    for (size_t i = 0; i < MAPPING_SLOTS && err == seL4_FailedLookup; i++) {
        /* save this so nothing else trashes the message register value */
        seL4_Word failed = seL4_MappingFailedLookupLevel();

        /* Assume the error was because we are missing a paging structure */
        ut_t *ut = ut_alloc_4k_untyped(NULL);
        if (ut == NULL) {
            ZF_LOGE("Out of 4k untyped");
            return -1;
        }

        /* figure out which cptr to use to retype into*/
        seL4_CPtr slot;
        if (used != NULL) {
            slot = free_slots[i];
            *used |= BIT(i);
        } else {
            slot = cspace_alloc_slot(cspace);
        }

        if (slot == seL4_CapNull) {
            ZF_LOGE("No cptr to alloc paging structure");
            return -1;
        }

        switch (failed) {
        case SEL4_MAPPING_LOOKUP_NO_PT:
            err = retype_map_pt(cspace, vspace, vaddr, ut->cap, slot);
            break;
        case SEL4_MAPPING_LOOKUP_NO_PD:
            err = retype_map_pd(cspace, vspace, vaddr, ut->cap, slot);
            break;

        case SEL4_MAPPING_LOOKUP_NO_PUD:
            err = retype_map_pud(cspace, vspace, vaddr, ut->cap, slot);
            break;
        }

        if (!err) {
            /* Try the mapping again */
            err = seL4_ARM_Page_Map(frame_cap, vspace, vaddr, rights, attr);
        }
    }

    return err;
}
static seL4_Error sos_map_frame(
    cspace_t *cspace, 
    page_metadata_t *page_metadata,
    seL4_Word vaddr,
    seL4_CapRights_t rights, 
    seL4_ARM_VMAttributes attr, 
    user_process_t *user_process
)
{
    // does not map the first page of the virtual address space
    // This prevents accidental usage of NULL 
    if (vaddr < PAGE_SIZE_4K) {
        ZF_LOGE("vaddr must not be within the first page of the virtual address space");
        return seL4_InvalidArgument;
    }

    return sos_shadow_map_frame(vaddr, page_metadata, cspace, user_process, rights, attr);
}

seL4_Error init_frame_metadata(cspace_t *cspace, frame_ref_t *out_frame, seL4_CPtr *out_frame_cptr, seL4_CapRights_t rights) {
    frame_ref_t frame = alloc_frame();
    if (frame == NULL_FRAME) {
        ZF_LOGE("Couldn't allocate additional frame");
        return seL4_NotEnoughMemory;
    }

    /* allocate a slot to duplicate the frame cap so we can map it into the application */
    seL4_CPtr frame_cptr = cspace_alloc_slot(cspace);
    if (frame_cptr == seL4_CapNull) {
        free_frame(frame);
        ZF_LOGE("Failed to alloc slot for extra frame cap");
        return seL4_NotEnoughMemory;
    }

    /* copy the frame cap into the slot */
    seL4_Error err = cspace_copy(cspace, frame_cptr, cspace, frame_page(frame), rights);
    if (err != seL4_NoError) {
        cspace_free_slot(cspace, frame_cptr);
        free_frame(frame);
        ZF_LOGE("Failed to copy cap, seL4_Error = %d\n", err);
        return err;
    }

    *out_frame = frame;
    *out_frame_cptr = frame_cptr;
    return seL4_NoError;
}

seL4_Error alloc_map_frame(cspace_t *cspace, uintptr_t vaddr, user_process_t *user_process, seL4_CapRights_t rights) {
    frame_ref_t frame;
    seL4_CPtr frame_cptr;

    seL4_Error err = init_frame_metadata(cspace, &frame, &frame_cptr, rights);
    if (err != seL4_NoError) {
        ZF_LOGE("Couldn't initialise frame metadata");
        return err;
    }

    page_metadata_t *page_metadata = malloc(sizeof(page_metadata_t));
    if (!page_metadata) {
        ZF_LOGE("Failed to allocate memory for page_metadata");
        return seL4_NotEnoughMemory;
    }

    uintptr_t aligned_vaddr = PAGE_ALIGN_4K(vaddr); /* seL4 page map methods only accepts vaddr that aligns with the size of a Page (4KB) */

    page_metadata->frame_ref = frame;
    page_metadata->frame_cap = frame_cptr;
    page_metadata->reference_bit = 1;
    page_metadata->pagefile_offset = -1;
    page_metadata->rights = rights;
    page_metadata->aligned_vaddr = aligned_vaddr;

    err = sos_map_frame(cspace, page_metadata, aligned_vaddr,
                    rights, seL4_ARM_Default_VMAttributes, user_process);

    if (err != seL4_NoError) {
        // delete the cap and free the allocated slot
        cspace_delete(cspace, frame_cptr);
        cspace_free_slot(cspace, frame_cptr);

        // free a physical frame
        free_frame(frame);

        // free the page_metadata
        free(page_metadata);

        ZF_LOGE("Unable to map extra frame for user app, seL4_Error = %d\n", err);
        return err;
    }

    user_process->size += 1;
    in_memory_pages_add(page_metadata);
    
    return seL4_NoError;
}

seL4_Error dealloc_unmap_frame(cspace_t *cspace, page_metadata_t *page) {
    seL4_Error err = seL4_ARM_Page_Unmap(page->frame_cap);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to unmap the page when deallocating the frames");
        return err;
    }

    err = cspace_delete(cspace, page->frame_cap);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to delete the copy of the frame cap");
        return err;
    }
    cspace_free_slot(cspace, page->frame_cap);
    free_frame(page->frame_ref);

    return seL4_NoError;
}

seL4_Error map_frame_cspace(cspace_t *cspace, seL4_CPtr frame_cap, seL4_CPtr vspace, seL4_Word vaddr,
                            seL4_CapRights_t rights, seL4_ARM_VMAttributes attr,
                            seL4_CPtr free_slots[MAPPING_SLOTS], seL4_Word *used)
{
    if (cspace == NULL) {
        ZF_LOGE("Invalid arguments");
        return -1;
    }
    return map_frame_impl(cspace, frame_cap, vspace, vaddr, rights, attr, free_slots, used);
}

seL4_Error map_frame(cspace_t *cspace, seL4_CPtr frame_cap, seL4_CPtr vspace, seL4_Word vaddr,
                     seL4_CapRights_t rights, seL4_ARM_VMAttributes attr)
{
    return map_frame_impl(cspace, frame_cap, vspace, vaddr, rights, attr, NULL, NULL);
}

static uintptr_t device_virt = SOS_DEVICE_START;

void *sos_map_device(cspace_t *cspace, uintptr_t addr, size_t size)
{
    assert(cspace != NULL);
    void *vstart = (void *) device_virt;

    for (uintptr_t curr = addr; curr < (addr + size); curr += PAGE_SIZE_4K) {
        ut_t *ut = ut_alloc_4k_device(curr);
        if (ut == NULL) {
            ZF_LOGE("Failed to find ut for phys address %p", (void *) curr);
            return NULL;
        }

        /* allocate a slot to retype into */
        seL4_CPtr frame = cspace_alloc_slot(cspace);
        if (frame == seL4_CapNull) {
            ZF_LOGE("Out of caps");
            return NULL;
        }

        /* retype */
        seL4_Error err = cspace_untyped_retype(cspace, ut->cap, frame, seL4_ARM_SmallPageObject,
                                               seL4_PageBits);
        if (err != seL4_NoError) {
            ZF_LOGE("Failed to retype %lx", (seL4_CPtr)ut->cap);
            cspace_free_slot(cspace, frame);
            return NULL;
        }

        /* map */
        err = map_frame(cspace, frame, seL4_CapInitThreadVSpace, device_virt, seL4_AllRights, false);
        if (err != seL4_NoError) {
            ZF_LOGE("Failed to map device frame at %p", (void *) device_virt);
            cspace_delete(cspace, frame);
            cspace_free_slot(cspace, frame);
            return NULL;
        }

        device_virt += PAGE_SIZE_4K;
    }

    return vstart;
}
