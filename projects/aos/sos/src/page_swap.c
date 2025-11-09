#include <sos/gen_config.h>
#include "page_swap.h"
#include <nfsc/libnfs.h>
#include "network.h"
#include "fcntl.h"
#include "mapping.h"
#include "cap_utils.h"

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
bool has_init_page_swap = false;


typedef struct nfs_open_pagefile_cb_args {
    seL4_CPtr ntfn;
} nfs_open_pagefile_cb_args_t;
static void nfs_open_pagefile_cb(int status, UNUSED struct nfs_context *nfs, void *data, UNUSED void *private_data);

typedef struct nfs_pwrite_pagefile_cb_args {
    seL4_CPtr ntfn;
    size_t bytes_written;
} nfs_pwrite_pagefile_cb_args_t;
static void nfs_pwrite_pagefile_cb(int status, UNUSED struct nfs_context *nfs, void *data, UNUSED void *private_data);

typedef struct nfs_pread_pagefile_cb_args {
    seL4_CPtr ntfn;
    size_t bytes_read;
    unsigned char* buf;
} nfs_pread_pagefile_cb_args_t;
static void nfs_pread_pagefile_cb(int status, UNUSED struct nfs_context *nfs, void *data, UNUSED void *private_data);

/**
 *  Read the content saved in pagefile, given the pagefile offset from page metadata.
 *  Then, writes exactly one page of data to buf.
 *  
 *  @param buf              buffer that will store the data read from the page
 *  @param page_metadata    page metadata that has the offset of their saved content from pagefile
 * 
 */
static void read_from_pagefile(unsigned char* buf, page_metadata_t *page_metadata);

/**
 *  Write the content of the given page to pagefile.
 *
 *  This function reads the data from the associated frame of the page, 
 *  writes that data to the pagefile starting at the available offset,
 *  then store this offset in the page metada.
 *
 *  @param page_metadata    page that has the content to be put in the pagefile
 * 
 */
static void write_to_pagefile(page_metadata_t *page_metadata);

/**
 * Return and pop the first element in the `free_pagefile_offsets` queue.
 * Also adds the new eof offset (= current eof_offset + `PAGE_SIZE_4K`) to the queue if the current eof offset is popped.
 * 
 * @return First element of the `free_pagefile_offsets` queue, implying an available offset in the pagefile.
 */
static size_t free_pagefile_offsets_pop();

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
    size_t eof_offset;
} offset_queue_t;

pages_queue_t in_memory_pages;
offset_queue_t free_pagefile_offsets;

SGLIB_DEFINE_QUEUE_FUNCTIONS(pages_queue_t, page_metadata_t *, arr, i, j, PAGES_QUEUE_MAX_SIZE)
SGLIB_DEFINE_QUEUE_FUNCTIONS(offset_queue_t, size_t, arr, i, j, OFFSET_QUEUE_MAX_SIZE)

int swap_to_mem(page_metadata_t *page, seL4_CPtr ntfn) {
    evict_page();
    
    frame_ref_t frame_ref = alloc_frame();
    assert(frame_ref != NULL_FRAME);

    // write the content of this page to the frame
    unsigned char *data = frame_data(frame_ref);
    read_from_pagefile(data, page);

    // update reference bit and offset
    page->reference_bit = 1;
    page->pagefile_offset = -1;
    
    in_memory_pages_add(page);
    return 0;
}

void in_memory_pages_add(page_metadata_t *page) {
    sglib_pages_queue_t_add(&in_memory_pages, page);
}

static size_t free_pagefile_offsets_pop() {
    size_t available_offset = sglib_offset_queue_t_first_element(&free_pagefile_offsets);
    sglib_offset_queue_t_delete_first(&free_pagefile_offsets);

    if (available_offset == free_pagefile_offsets.eof_offset) {/* eof offset is popped, need to update eof offset*/
        free_pagefile_offsets.eof_offset += PAGE_SIZE_4K;
        sglib_offset_queue_t_add(&free_pagefile_offsets, free_pagefile_offsets.eof_offset);
    }

    return available_offset;
}

static void write_to_pagefile(page_metadata_t *page_metadata) {
    unsigned char* frame_content = frame_data(page_metadata->frame_ref);
    // offset to the available space in pagefile
    size_t available_offset = free_pagefile_offsets_pop();

    // write content to pagefile at offset, must ensure all bytes are written
    size_t total_bytes_written = 0;

    // create a notification object
    seL4_CPtr ntfn;
    ut_t *ut = create_cap(&ntfn, seL4_NotificationObject, seL4_NotificationBits);

    nfs_pwrite_pagefile_cb_args_t pwrite_cb_args = {.ntfn = ntfn};
    
    while (total_bytes_written < PAGE_SIZE_4K) {
        size_t bytes_to_write = PAGE_SIZE_4K - total_bytes_written;
        size_t offset = available_offset + total_bytes_written;

        int ret = nfs_pwrite_async( nfs, pagefile_fh, offset, bytes_to_write, 
                                    (const void*)(frame_content + total_bytes_written), 
                                    nfs_pwrite_pagefile_cb, &pwrite_cb_args); 
        ZF_LOGF_IF(ret != 0, "queuing pwrite pagefile failed: %s", nfs_get_error(nfs));
        
        seL4_Wait(ntfn, NULL);
        
        total_bytes_written += pwrite_cb_args.bytes_written;
    }

    // free the notification object
    free_cap(ut, ntfn);

    // save the offset to page_metadata
    page_metadata->pagefile_offset = available_offset;
}

static void read_from_pagefile(unsigned char* buf, page_metadata_t *page_metadata) {
    size_t pagefile_offset = page_metadata->pagefile_offset;

    size_t total_bytes_read = 0;

    // create a notification object
    seL4_CPtr ntfn;
    ut_t *ut = create_cap(&ntfn, seL4_NotificationObject, seL4_NotificationBits);
    
    nfs_pread_pagefile_cb_args_t pread_cb_args = {.ntfn = ntfn};
    
    while (total_bytes_read < PAGE_SIZE_4K) {
        size_t bytes_to_read = PAGE_SIZE_4K - total_bytes_read;
        size_t offset = pagefile_offset + total_bytes_read;

        pread_cb_args.buf = buf + total_bytes_read;

        int ret = nfs_pread_async(nfs, pagefile_fh, offset, bytes_to_read, nfs_pread_pagefile_cb, &pread_cb_args);
        ZF_LOGF_IF(ret != 0, "queuing pread pagefile failed: %s", nfs_get_error(nfs));
        
        seL4_Wait(ntfn, NULL);
        
        total_bytes_read += pread_cb_args.bytes_read;
    }

    // free the notification object
    free_cap(ut, ntfn);
}

void evict_page() {
    while (!sglib_pages_queue_t_is_empty(&in_memory_pages)) {
        page_metadata_t *page = sglib_pages_queue_t_first_element(&in_memory_pages);
        sglib_pages_queue_t_delete_first(&in_memory_pages);

        if (page->reference_bit == 1) { /* give it a second chance */
            in_memory_pages_add(page);
            page->reference_bit = 0;

            // unmap the page so that we can simulate the reference bit via vm fault
            seL4_Error err = seL4_ARM_Page_Unmap(page->frame_cap);
            ZF_LOGF_IF(err != seL4_NoError, "Unable to unmap the page, seL4_Error = %d\n", err);
        } else if (page->reference_bit == 0) {
            write_to_pagefile(page);

            // zero out the frame
            unsigned char *data = frame_data(page->frame_ref);
            memset(data, 0, PAGE_SIZE_4K);

            // destruct page_metadata
            seL4_Error err = dealloc_unmap_frame(&cspace, page);
            ZF_LOGF_IF(err != seL4_NoError, "Unable to deallocate and unmap the page, seL4_Error = %d\n", err);
        }
    }
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
    free_pagefile_offsets.eof_offset = 0;

    int ret = nfs_open_async(nfs, "pagefile", O_RDWR | O_CREAT, nfs_open_pagefile_cb, NULL); 
    ZF_LOGF_IF(ret != 0, "queuing open pagefile failed: %s", nfs_get_error(nfs));
}

void nfs_open_pagefile_cb(int status, UNUSED struct nfs_context *nfs, void *data,
                 UNUSED void *private_data) {
    if (status < 0) {
        ZF_LOGF("open pagefile failed with \"%s\"\n", (char *)data);
    }

    pagefile_fh = (struct nfsfh*)data;
    has_init_page_swap = true;
}

void nfs_pwrite_pagefile_cb(int status, UNUSED struct nfs_context *nfs, void *data, 
                  UNUSED void *private_data)
{
    if (status < 0) {
        ZF_LOGF("pwrite to pagefile failed with \"%s\"\n", (char *)data);
    }

    nfs_pwrite_pagefile_cb_args_t *args = private_data;
    args->bytes_written = status;
    seL4_Signal(args->ntfn);
}

void nfs_pread_pagefile_cb(int status, UNUSED struct nfs_context *nfs, void *data, 
                  UNUSED void *private_data)
{
    if (status < 0) {
        ZF_LOGF("pread to pagefile failed with \"%s\"\n", (char *)data);
    }

    nfs_pread_pagefile_cb_args_t *args = private_data;
    args->bytes_read = status;
    args->buf = data;

    seL4_Signal(args->ntfn);
}