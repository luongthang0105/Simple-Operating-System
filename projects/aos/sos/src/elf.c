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
#include <utils/util.h>
#include <stdbool.h>
#include <sel4/sel4.h>
#include <elf/elf.h>
#include <string.h>
#include <assert.h>
#include <cspace/cspace.h>

#include "frame_table.h"
#include "ut.h"
#include "mapping.h"
#include "elfload.h"

/*
 * Convert ELF permissions into seL4 permissions.
 */
static inline seL4_CapRights_t get_sel4_rights_from_elf(unsigned long permissions)
{
    bool canRead = permissions & PF_R || permissions & PF_X;
    bool canWrite = permissions & PF_W;

    if (!canRead && !canWrite) {
        return seL4_AllRights;
    }

    return seL4_CapRights_new(false, false, canRead, canWrite);
}

/*
 * Load an elf segment into the given vspace.
 *
 * TODO: The current implementation maps the frames into the loader vspace AND the target vspace
 *       and leaves them there. Additionally, if the current implementation fails, it does not
 *       clean up after itself.
 *
 *       This is insufficient, as you will run out of resouces quickly, and will be completely fixed
 *       throughout the duration of the project, as different milestones are completed.
 *
 *       Be *very* careful when editing this code. Most students will experience at least one elf-loading
 *       bug.
 *
 * The content to load is either zeros or the content of the ELF
 * file itself, or both.
 * The split between file content and zeros is a follows.
 *
 * File content: [dst, dst + file_size)
 * Zeros:        [dst + file_size, dst + segment_size)
 *
 * Note: if file_size == segment_size, there is no zero-filled region.
 * Note: if file_size == 0, the whole segment is just zero filled.
 *
 * @param cspace        of the loader, to allocate slots with
 * @param loader        vspace of the loader
 * @param loadee        vspace to load the segment in to
 * @param src           pointer to the content to load
 * @param segment_size  size of segment to load
 * @param file_size     end of section that should be zero'd
 * @param dst           destination base virtual address to load
 * @param permissions   for the mappings in this segment
 * @return
 *
 */
static int load_segment_into_vspace(cspace_t *cspace, const char *src, size_t segment_size,
                                    size_t file_size, uintptr_t dst, seL4_CapRights_t permissions, user_process_t *user_process)
{
    assert(file_size <= segment_size);

    /* We work a page at a time in the destination vspace. */
    unsigned int pos = 0;
    seL4_Error err = seL4_NoError;
    while (pos < segment_size) {
        uintptr_t loadee_vaddr = (ROUND_DOWN(dst, PAGE_SIZE_4K));

        err = alloc_map_frame(cspace, loadee_vaddr, user_process, permissions);
        /* A frame has already been mapped at this address. This occurs when segments overlap in
         * the same frame, which is permitted by the standard. That's fine as we
         * leave all the frames mapped in, and this one is already mapped. 
         * 
         * Now retrieve the frame and continue on to do the write.
         *
         * Note that while the standard permits segments to overlap, this should not occur if the segments
         * have different permissions - you should check this and return an error if this case is detected. */
        if (err != seL4_NoError) {
            ZF_LOGE("Failed to map into loadee vspace at %p, error %u", (void *) loadee_vaddr, err);
            return -1;
        }

        page_metadata_t *page_metadata = find_page(loadee_vaddr, user_process->page_global_directory);
        
        /* finally copy the data */
        unsigned char *loader_data = frame_data(page_metadata->frame_ref);

        /* Write any zeroes at the start of the block. */
        size_t leading_zeroes = dst % PAGE_SIZE_4K;
        memset(loader_data, 0, leading_zeroes);
        loader_data += leading_zeroes;

        /* Copy the data from the source. */
        size_t segment_bytes = PAGE_SIZE_4K - leading_zeroes;
        if (pos < file_size) {
            size_t file_bytes = MIN(segment_bytes, file_size - pos);
            memcpy(loader_data, src, file_bytes);
            loader_data += file_bytes;
            
            /* Fill in the end of the frame with zereos */
            size_t trailing_zeroes = PAGE_SIZE_4K - (leading_zeroes + file_bytes);
            memset(loader_data, 0, trailing_zeroes);
        } else {
            memset(loader_data, 0, segment_bytes);
        }

        pos += segment_bytes;
        dst += segment_bytes;
        src += segment_bytes;
    }
    return 0;
}

int elf_load(cspace_t *cspace, elf_t *elf_file, user_process_t *user_process)
{
    uintptr_t heap_vaddr_base;
    int num_headers = elf_getNumProgramHeaders(elf_file);
    for (int i = 0; i < num_headers; i++) {

        /* Skip non-loadable segments (such as debugging data). */
        if (elf_getProgramHeaderType(elf_file, i) != PT_LOAD) {
            continue;
        }

        /* Fetch information about this segment. */
        const char *source_addr = elf_file->elfFile + elf_getProgramHeaderOffset(elf_file, i);
        size_t file_size = elf_getProgramHeaderFileSize(elf_file, i);
        size_t segment_size = elf_getProgramHeaderMemorySize(elf_file, i);
        uintptr_t vaddr = elf_getProgramHeaderVaddr(elf_file, i);
        seL4_Word flags = elf_getProgramHeaderFlags(elf_file, i);

        // heap_vaddr_base must be after the last loaded segment 
        // we currently assume the last segment to be the DATA segment!
        heap_vaddr_base = (ROUND_UP(vaddr + segment_size, PAGE_SIZE_4K));

        /* Create a region for this segment */
        if (add_vm_region(user_process->vm_regions, vaddr, segment_size, get_sel4_rights_from_elf(flags), false) == NULL) {
            ZF_LOGE("Unable to create a region");
            return -1;
        }
        
        /* Copy it across into the vspace. */
        ZF_LOGE(" * Loading segment %p-->%p\n", (void *) vaddr, (void *)(vaddr + segment_size));
        int err = load_segment_into_vspace(cspace, source_addr, segment_size, file_size, vaddr,
            get_sel4_rights_from_elf(flags), user_process);
            if (err) {
                ZF_LOGE("Elf loading failed!");
                return -1;
            }
        }
        
        
    /* Create a heap region */
    user_process->heap_region = add_vm_region(user_process->vm_regions, heap_vaddr_base, 0, seL4_ReadWrite, false);
    if (user_process->heap_region == NULL) {
        ZF_LOGE("Unable to create a heap region");
        return -1;
    }

    return 0;
}
