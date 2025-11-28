#include <sos/gen_config.h>
#include "page_swap.h"
#include <nfsc/libnfs.h>
#include "network.h"
#include "fcntl.h"
#include "mapping.h"
#include "cap_utils.h"
#include "backtrace.h"
#include "threads.h"

extern cspace_t cspace;
struct nfsfh *pagefile_fh; /* NFS file handle for pagefile */
struct nfs_context *nfs;
bool has_init_page_swap = false;

pages_queue_t in_memory_pages;
sync_recursive_mutex_t *in_memory_pages_mutex;

offset_queue_t free_pagefile_offsets;
sync_recursive_mutex_t *free_pagefile_offsets_mutex;

SGLIB_DEFINE_QUEUE_FUNCTIONS(pages_queue_t, page_metadata_t *, arr, i, j, PAGES_QUEUE_MAX_SIZE)
SGLIB_DEFINE_QUEUE_FUNCTIONS(offset_queue_t, size_t, arr, i, j, OFFSET_QUEUE_MAX_SIZE)

typedef struct nfs_open_pagefile_cb_args {
    uint32_t thread_index;
    pid_t expected_pid;
} nfs_open_pagefile_cb_args_t;
static void nfs_open_pagefile_cb(int status, UNUSED struct nfs_context *nfs, void *data, UNUSED void *private_data);

typedef struct nfs_pwrite_pagefile_cb_args {
    uint32_t thread_index;
    pid_t expected_pid;
    size_t bytes_written;
} nfs_pwrite_pagefile_cb_args_t;
static void nfs_pwrite_pagefile_cb(int status, UNUSED struct nfs_context *nfs, void *data, UNUSED void *private_data);

typedef struct nfs_pread_pagefile_cb_args {
    uint32_t thread_index;
    pid_t expected_pid;
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
static void free_pagefile_offsets_add(size_t new_offset);

int swap_to_mem(page_metadata_t *page, seL4_CPtr vspace) {   
    sync_recursive_mutex_lock(in_memory_pages_mutex);

    frame_ref_t frame_ref = alloc_frame();
    assert(frame_ref != NULL_FRAME);

    // write the content of this page to the frame
    unsigned char *data = frame_data(frame_ref);
    read_from_pagefile(data, page);

    /* return pagefile offset to queue */
    /* TODO: this is not what we want, we want to keep the content in pagefile (and the offset to it) until the process is destroy */
    free_pagefile_offsets_add(page->pagefile_offset);

    /* allocate a slot to duplicate the frame cap so we can map it into the application */
    seL4_CPtr frame_cptr = cspace_alloc_slot(&cspace);
    if (frame_cptr == seL4_CapNull) {
        free_frame(frame_ref);
        ZF_LOGE("Failed to alloc slot for extra frame cap");
        sync_recursive_mutex_unlock(in_memory_pages_mutex);

        return seL4_NotEnoughMemory;
    }

    /* copy the frame cap into the slot */
    seL4_Error err = cspace_copy(&cspace, frame_cptr, &cspace, frame_page(frame_ref), page->rights);
    if (err != seL4_NoError) {
        cspace_free_slot(&cspace, frame_cptr);
        free_frame(frame_ref);
        ZF_LOGE("Failed to copy cap, seL4_Error = %d\n", err);
        sync_recursive_mutex_unlock(in_memory_pages_mutex);

        return err;
    }

    /*  Update the page's status. The page's virtual address (vaddr) and rights do not need updating here:
        - vaddr is fixed and always page-aligned
        - rights are determined by the region the page belongs to and must remain consistent
    */
    page->frame_ref = frame_ref;
    page->frame_cap = frame_cptr;
    page->reference_bit = 1;
    page->pagefile_offset = -1;

    err = reference_page(page, vspace, page->aligned_vaddr, page->rights);
    if (err != seL4_NoError) {
        cspace_free_slot(&cspace, frame_cptr);
        free_frame(frame_ref);
        ZF_LOGE("Failed to reference page when swap page from disk to memory, seL4_Error = %d\n", err);
        sync_recursive_mutex_unlock(in_memory_pages_mutex);
        return err;
    }

    in_memory_pages_add(page);

    sync_recursive_mutex_unlock(in_memory_pages_mutex);

    return 0;
}

void in_memory_pages_add(page_metadata_t *page) {
    sync_recursive_mutex_lock(in_memory_pages_mutex);
    sglib_pages_queue_t_add(&in_memory_pages, page);
    sync_recursive_mutex_unlock(in_memory_pages_mutex);
}

page_metadata_t *in_memory_pages_pop() {
    sync_recursive_mutex_lock(in_memory_pages_mutex);
    
    page_metadata_t *page = sglib_pages_queue_t_first_element(&in_memory_pages);
    sglib_pages_queue_t_delete_first(&in_memory_pages);
    
    sync_recursive_mutex_unlock(in_memory_pages_mutex);
    return page;
}

static size_t free_pagefile_offsets_pop() {
    sync_recursive_mutex_lock(free_pagefile_offsets_mutex);
    
    if (!sglib_offset_queue_t_is_empty(&free_pagefile_offsets)) {
        size_t available_offset = sglib_offset_queue_t_first_element(&free_pagefile_offsets);
        sglib_offset_queue_t_delete_first(&free_pagefile_offsets);

        if (available_offset == free_pagefile_offsets.eof_offset) {/* eof offset is popped, need to update eof offset*/
            free_pagefile_offsets.eof_offset += PAGE_SIZE_4K;
            sglib_offset_queue_t_add(&free_pagefile_offsets, free_pagefile_offsets.eof_offset);
        }
        sync_recursive_mutex_unlock(free_pagefile_offsets_mutex);
        return available_offset;
    }

    ZF_LOGE("Free pagefile offset queue is empty!");
    sync_recursive_mutex_unlock(free_pagefile_offsets_mutex);

    return 0;
}

void free_pagefile_offsets_add(size_t new_offset) {
    sync_recursive_mutex_lock(free_pagefile_offsets_mutex);
    sglib_offset_queue_t_add(&free_pagefile_offsets, new_offset);
    sync_recursive_mutex_unlock(free_pagefile_offsets_mutex);
}

static void write_to_pagefile(page_metadata_t *page_metadata) {
    unsigned char* frame_content = frame_data(page_metadata->frame_ref);
    // offset to the available space in pagefile
    size_t available_offset = free_pagefile_offsets_pop();

    // write content to pagefile at offset, must ensure all bytes are written
    size_t total_bytes_written = 0;

    nfs_pwrite_pagefile_cb_args_t pwrite_cb_args = {
        .thread_index = current_thread->thread_id,
        .expected_pid = current_thread->assigned_pid
    };
    
    while (total_bytes_written < PAGE_SIZE_4K) {
        pwrite_cb_args.bytes_written = 0;
        
        size_t bytes_to_write = PAGE_SIZE_4K - total_bytes_written;
        size_t offset = available_offset + total_bytes_written;

        int ret = nfs_pwrite_async( nfs, pagefile_fh, offset, bytes_to_write, 
                                    frame_content + total_bytes_written, 
                                    nfs_pwrite_pagefile_cb, &pwrite_cb_args);
        ZF_LOGF_IF(ret != 0, "queuing pwrite pagefile failed: %s", nfs_get_error(nfs));
        
        seL4_Wait(current_thread->ntfn, NULL);
        
        total_bytes_written += pwrite_cb_args.bytes_written;
    }

    // save the offset to page_metadata
    page_metadata->pagefile_offset = available_offset;
}

static void read_from_pagefile(unsigned char* buf, page_metadata_t *page_metadata) {
    size_t pagefile_offset = page_metadata->pagefile_offset;

    size_t total_bytes_read = 0;

    nfs_pread_pagefile_cb_args_t pread_cb_args = {
        .thread_index = current_thread->thread_id,
        .expected_pid = current_thread->assigned_pid
    };
    
    while (total_bytes_read < PAGE_SIZE_4K) {
        size_t bytes_to_read = PAGE_SIZE_4K - total_bytes_read;
        size_t offset = pagefile_offset + total_bytes_read;

        pread_cb_args.buf = buf + total_bytes_read;

        int ret = nfs_pread_async(nfs, pagefile_fh, offset, bytes_to_read, nfs_pread_pagefile_cb, &pread_cb_args);
        ZF_LOGF_IF(ret != 0, "queuing pread pagefile failed: %s", nfs_get_error(nfs));
        
        seL4_Wait(current_thread->ntfn, NULL);
        
        total_bytes_read += pread_cb_args.bytes_read;
    }
}

void evict_page() {
    sync_recursive_mutex_lock(in_memory_pages_mutex);

    while (!sglib_pages_queue_t_is_empty(&in_memory_pages)) {
        page_metadata_t *page = sglib_pages_queue_t_first_element(&in_memory_pages);
        sglib_pages_queue_t_delete_first(&in_memory_pages);
        
        if (page->reference_bit == 1) { /* give it a second chance */
            page->reference_bit = 0;
            sglib_pages_queue_t_add(&in_memory_pages, page);
            
            // unmap the page so that we can simulate the reference bit via vm fault
            seL4_Error err = seL4_ARM_Page_Unmap(page->frame_cap);
            ZF_LOGF_IF(err != seL4_NoError, "Unable to unmap the page, seL4_Error = %d\n", err);

        } else if (page->reference_bit == 0) {
            write_to_pagefile(page);

            // destruct page_metadata
            seL4_Error err = dealloc_unmap_frame(&cspace, page);
            ZF_LOGF_IF(err != seL4_NoError, "Unable to deallocate and unmap the page, seL4_Error = %d\n", err);
            break;
        }
    }

    sync_recursive_mutex_unlock(in_memory_pages_mutex);
}

seL4_Error reference_page(page_metadata_t *page, seL4_CPtr vspace, seL4_Word vaddr, seL4_CapRights_t rights) {
    page->reference_bit = 1;
    seL4_Error err = seL4_ARM_Page_Map(page->frame_cap, vspace, PAGE_ALIGN_4K(vaddr), rights, seL4_ARM_Default_VMAttributes);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to perform page map. seL4_Error = %d", err);
        return CSPACE_ERROR;
    }
    return seL4_NoError;
}

void init_page_swap() {
    // initialize the mutexes
    in_memory_pages_mutex = malloc(sizeof(sync_recursive_mutex_t));
    free_pagefile_offsets_mutex = malloc(sizeof(sync_recursive_mutex_t));

    sync_recursive_mutex_new(in_memory_pages_mutex);
    sync_recursive_mutex_new(free_pagefile_offsets_mutex);

    // open the pagefile
    nfs = get_nfs_context();

    sglib_offset_queue_t_add(&free_pagefile_offsets, 0);
    free_pagefile_offsets.eof_offset = 0;

    int ret = nfs_open_async(nfs, "pagefile", O_RDWR | O_CREAT | O_TRUNC, nfs_open_pagefile_cb, NULL); 
    ZF_LOGF_IF(ret != 0, "queuing open pagefile failed: %s", nfs_get_error(nfs));
}

void nfs_open_pagefile_cb(int status, UNUSED struct nfs_context *nfs, void *data,
                 UNUSED void *private_data) {
    if (status < 0) {
        ZF_LOGF("open pagefile failed with \"%s\"\n", (char *)data);
        return;
    }

    pagefile_fh = (struct nfsfh*)data;
    has_init_page_swap = true;
}

void nfs_pwrite_pagefile_cb(int status, UNUSED struct nfs_context *nfs, void *data, 
                  UNUSED void *private_data)
{   
    sync_recursive_mutex_lock(worker_threads_mutex);
    nfs_pwrite_pagefile_cb_args_t *args = private_data;

    pid_t expected_id = args->expected_pid;
    uint32_t thread_index = args->thread_index;
    sos_thread_t *worker_thread = worker_threads[thread_index];

    if (expected_id != worker_thread->assigned_pid) {
        sync_recursive_mutex_unlock(worker_threads_mutex);
        return;
    }

    if (status < 0) {
        ZF_LOGE("pwrite to pagefile failed with \"%s\"\n", (char *)data);
        args->bytes_written = 0;
        seL4_Signal(worker_thread->ntfn);
        sync_recursive_mutex_unlock(worker_threads_mutex);
        return;
    }

    args->bytes_written = status;
    seL4_Signal(worker_thread->ntfn);
    sync_recursive_mutex_unlock(worker_threads_mutex);
}

void nfs_pread_pagefile_cb(int status, UNUSED struct nfs_context *nfs, void *data, 
                  UNUSED void *private_data)
{   
    sync_recursive_mutex_lock(worker_threads_mutex);
    
    nfs_pread_pagefile_cb_args_t *args = private_data;
    pid_t expected_id = args->expected_pid;
    uint32_t thread_index = args->thread_index;
    sos_thread_t *worker_thread = worker_threads[thread_index];

    if (expected_id != worker_thread->assigned_pid) {
        sync_recursive_mutex_unlock(worker_threads_mutex);
        return;
    }

    if (status < 0) {
        ZF_LOGF("pread from pagefile failed with \"%s\"\n", (char *)data);
        args->bytes_read = 0;
        seL4_Signal(worker_thread->ntfn);
        sync_recursive_mutex_unlock(worker_threads_mutex);
        return;
    }

    args->bytes_read = status;

    if (status > 0) {
        memcpy((void *)args->buf, (const void*)data, status);
    }

    seL4_Signal(worker_thread->ntfn);
    sync_recursive_mutex_unlock(worker_threads_mutex);

}