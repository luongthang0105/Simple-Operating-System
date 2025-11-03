#include <sos/gen_config.h>
#include "page_swap.h"
#include <nfsc/libnfs.h>
#include "network.h"
#include "fcntl.h"
#include "mapping.h"

#ifdef CONFIG_SOS_FRAME_LIMIT
#define PAGES_QUEUE_MAX_SIZE    ((CONFIG_SOS_FRAME_LIMIT == 0ul) ? (1 << 19) : CONFIG_SOS_FRAME_LIMIT)
#define OFFSET_QUEUE_MAX_SIZE   ((CONFIG_SOS_FRAME_LIMIT == 0ul) ? (1 << 19) : (CONFIG_SOS_FRAME_LIMIT * 10))
#else
#define PAGES_QUEUE_MAX_SIZE (1 << 19)
const size_t OFFSET_QUEUE_MAX_SIZE = (1 << 19);
#endif

extern cspace_t cspace;
struct nfsfh *pagefile_fh; /* NFS file handle for pagefile */
struct nfs_context *nfs;

typedef struct nfs_open_cb_args {
    seL4_CPtr ntfn;
} nfs_open_cb_args_t;
static void nfs_open_cb(int status, UNUSED struct nfs_context *nfs, void *data, UNUSED void *private_data);

typedef struct nfs_pwrite_cb_args {
    seL4_CPtr ntfn;
    size_t bytes_written;
} nfs_pwrite_cb_args_t;
static void nfs_pwrite_cb(int status, UNUSED struct nfs_context *nfs, void *data, UNUSED void *private_data);

typedef struct nfs_pread_cb_args {
    seL4_CPtr ntfn;
    size_t bytes_read;
    unsigned char* buf;
} nfs_pread_cb_args_t;
static void nfs_pread_cb(int status, UNUSED struct nfs_context *nfs, void *data, UNUSED void *private_data);

typedef struct pages_queue
{
    page_metadata_t *arr[PAGES_QUEUE_MAX_SIZE];
    size_t i;
    size_t j;
} pages_queue_t;

/*  A queue that contains the currently free space in the pagefile (determined by the offset)
It should always have at least one item in it, which is the offset to the end of the file.
The initial item in the queue is offset 0.
*/
typedef struct free_pagefile_offsets {
    size_t arr[OFFSET_QUEUE_MAX_SIZE];
    size_t i;
    size_t j;
} offset_queue_t;

pages_queue_t in_memory_pages;
offset_queue_t free_pagefile_offsets;

SGLIB_DEFINE_QUEUE_FUNCTIONS(pages_queue_t, page_metadata_t *, arr, i, j, PAGES_QUEUE_MAX_SIZE)
SGLIB_DEFINE_QUEUE_FUNCTIONS(offset_queue_t, size_t, arr, i, j, OFFSET_QUEUE_MAX_SIZE)

int swap_to_mem(page_metadata_t *page, seL4_CPtr ntfn) {
    // read the content of this page from the disk

    // get the freed frame
    
    // write the content of this page to the frame
    // write_to_pagefile(/*the page metadata of the page that gets evicted*/, ntfn);

    // update reference bit and offset
    page->reference_bit = 1;
    page->pagefile_offset = -1;
    sglib_pages_queue_t_add(&in_memory_pages, page);
    
    return 0;
}

/**
 *  Read the content of the given page, and write it to the disk.
 *
 *  This function reads the data from the associated frame of the page, 
 *  writes that data to the pagefile starting at the available offset,
 *  then store this offset in the page metada.
 *
 *  @param page_metadata    page that has the content to be put in the disk
 *  @param ntfn             notification cap from worker thread to wait until write operation finished
 * 
 */
static void write_to_pagefile(page_metadata_t *page_metadata, seL4_CPtr ntfn) {
    unsigned char* frame_content = frame_data(page_metadata->frame_ref);
    // offset to the available space in pagefile
    size_t available_offset = sglib_offset_queue_t_first_element(&free_pagefile_offsets);
    sglib_offset_queue_t_delete_first(&free_pagefile_offsets);

    // add the next available offset
    sglib_offset_queue_t_add(&free_pagefile_offsets, available_offset + PAGE_SIZE_4K);

    // write content to pagefile at offset, must ensure all bytes are written
    size_t total_bytes_written = 0;
    nfs_pwrite_cb_args_t pwrite_cb_args = {.ntfn = ntfn};
    
    while (total_bytes_written < PAGE_SIZE_4K) {
        size_t bytes_to_write = PAGE_SIZE_4K - total_bytes_written;
        size_t offset = available_offset + total_bytes_written;

        int ret = nfs_pwrite_async( nfs, pagefile_fh, offset, bytes_to_write, 
                                    (const void*)(frame_content + total_bytes_written), 
                                    nfs_pwrite_cb, &pwrite_cb_args); 
        ZF_LOGF_IF(ret != 0, "queuing pwrite pagefile failed: %s", nfs_get_error(nfs));
        
        seL4_Wait(ntfn, NULL);
        
        total_bytes_written += pwrite_cb_args.bytes_written;
    }

    // save the offset to page_metadata
    page_metadata->pagefile_offset = available_offset;
}

/**
 *  Read the content saved in pagefile, given the pagefile offset from page metadata.
 *  Then, writes exactly one page of data to buf.
 *
 *  @param page_metadata    page that reads their saved content from disk
 *  @param ntfn             notification cap from worker thread to wait until write operation finished
 * 
 */
static void read_from_pagefile(unsigned char* buf, page_metadata_t *page_metadata, seL4_CPtr ntfn) {
    size_t pagefile_offset = page_metadata->pagefile_offset;

    size_t total_bytes_read = 0;
    nfs_pread_cb_args_t pread_cb_args = {.ntfn = ntfn};
    
    while (total_bytes_read < PAGE_SIZE_4K) {
        size_t bytes_to_read = PAGE_SIZE_4K - total_bytes_read;
        size_t offset = pagefile_offset + total_bytes_read;

        pread_cb_args.buf = buf + total_bytes_read;

        int ret = nfs_pread_async(nfs, pagefile_fh, offset, bytes_to_read, nfs_pread_cb, &pread_cb_args);
        ZF_LOGF_IF(ret != 0, "queuing pread pagefile failed: %s", nfs_get_error(nfs));
        
        seL4_Wait(ntfn, NULL);
        
        total_bytes_read += pread_cb_args.bytes_read;
    }
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

void init_page_swap() {
    nfs = get_nfs_context();
    sglib_offset_queue_t_add(&free_pagefile_offsets, 0);

    // allocate a ntfn to wait till pagefile finished opening
    seL4_CPtr ntfn;
    ut_t *ut = alloc_retype(&ntfn, seL4_NotificationObject, seL4_NotificationBits);
    ZF_LOGF_IF(!ut, "No memory for notification object");

    nfs_open_cb_args_t cb_args = {.ntfn = ntfn};
    int ret = nfs_open_async(nfs, "pagefile", O_RDWR | O_CREAT, nfs_open_cb, &cb_args); 
    ZF_LOGF_IF(ret != 0, "queuing open pagefile failed: %s", nfs_get_error(nfs));

    seL4_Wait(ntfn, NULL);

    // free up allocations for ntfn
    seL4_Error del_error = cspace_delete(&cspace, ntfn);
    if (del_error != seL4_NoError) {
        ZF_LOGF("Failed to delete ntfn cap, seL4_Error = %d", del_error);
    }
    cspace_free_slot(&cspace, ntfn);
    ut_free(ut);
}

void nfs_open_cb(int status, UNUSED struct nfs_context *nfs, void *data,
                 UNUSED void *private_data) {
    if (status < 0) {
        ZF_LOGF("open pagefile failed with \"%s\"\n", (char *)data);
    }

    seL4_Signal(((nfs_open_cb_args_t*)private_data)->ntfn);
    pagefile_fh = (struct nfsfh*)data;
}

void nfs_pwrite_cb(int status, UNUSED struct nfs_context *nfs, void *data, 
                  UNUSED void *private_data)
{
    if (status < 0) {
        ZF_LOGF("pwrite to pagefile failed with \"%s\"\n", (char *)data);
    }

    nfs_pwrite_cb_args_t *args = private_data;
    args->bytes_written = status;
    seL4_Signal(args->ntfn);
}

void nfs_pread_cb(int status, UNUSED struct nfs_context *nfs, void *data, 
                  UNUSED void *private_data)
{
    if (status < 0) {
        ZF_LOGF("pread to pagefile failed with \"%s\"\n", (char *)data);
    }

    nfs_pread_cb_args_t *args = private_data;
    args->bytes_read = status;
    args->buf = data;

    seL4_Signal(args->ntfn);
}