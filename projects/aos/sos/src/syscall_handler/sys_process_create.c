#include <aos/sel4_zf_logif.h>
#include "sys_process_create.h"
#include "../user_process.h"
#include "../threads.h"
#include "fcntl.h"
#include "elf/elf.h"
#include "../network.h"
#include "../vmem_layout.h"
#include "../pagetable.h"
#include "../user_app.h"
#include "../sched_ctrl.h"
#include "../elfload.h"
#include <aos/debug.h>
#include "string.h"
#include "../nfs_wrapper.h"
#include <clock/clock.h>
#include "../utils.h"
#include <elf/elf64.h>
#include "sys_mmap.h"

/* The number of additional stack pages to provide to the initial
 * process */
#define INITIAL_PROCESS_STACK_PAGES 10
#define MAX_PROCESS_STACK_PAGES 9*29

typedef struct nfs_open_cb_args {
    uint32_t thread_index;
    struct nfsfh *nfsfh;
    int err;
} nfs_open_cb_args_t;

static void nfs_open_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{   
    nfs_open_cb_args_t *ret_private_data = (nfs_open_cb_args_t *)private_data;
    int thread_index = ret_private_data->thread_index;
    user_process_t *user_process = get_current_user_process_by_thread(thread_index);
    
    if (status < 0)
    {
        ZF_LOGE("error: %d, error msg: %s\n", status, (char *)data);
        ret_private_data->err = status;
        seL4_Signal(worker_threads[thread_index]->ntfn);
        return;
    }

    ret_private_data->nfsfh = (struct nfsfh *)data;

    seL4_Signal(worker_threads[thread_index]->ntfn);
}


static struct nfsfh* open_elf(uintptr_t path_vaddr, size_t path_len, char** path) {
    int thread_index = current_thread->thread_id;

    char *temp_path_buf = malloc(path_len + 1);
    if (temp_path_buf == NULL)
    {
        ZF_LOGE("Failed to allocate memory for temp_path_buf");
        seL4_SetMR(0, -1);
        return NULL;
    }
    temp_path_buf[path_len] = '\0';

    int status = copy_from_user((void *)temp_path_buf, (void *)path_vaddr, path_len);
    if (status == -1)
    {
        ZF_LOGE("Failed to copy path from user");
        free(temp_path_buf);
        seL4_SetMR(0, -1);
        return NULL;
    }

    struct nfs_context *nfs_context = get_nfs_context();

    nfs_open_cb_args_t private_data = {
        .thread_index = thread_index,
        .err = 0,
        .nfsfh = NULL
    };

    int err = nfs_open_async(nfs_context, temp_path_buf, O_RDONLY, nfs_open_cb, &private_data);

    if (err)
    {
        ZF_LOGE("An error occured when trying to queue the command nfs_open_async. The callback will not be invoked.");
        free(temp_path_buf);
        return NULL;
    }

    seL4_Wait(worker_threads[thread_index]->ntfn, NULL);

    *path = temp_path_buf;
    return private_data.nfsfh;
}

static int read_elf_header(struct nfsfh* elf_fh, unsigned char** elf_header_data) {
    *elf_header_data = malloc(PAGE_SIZE_4K);
    
    nfs_pread_cb_args_t args = {
        .thread_index = current_thread->thread_id,
        .read_buf = *elf_header_data,
        .expected_pid = current_thread->assigned_pid
    };
    
    int status = nfs_pread_wrapper(elf_fh, &args, 0, PAGE_SIZE_4K);

    if (status == -1) {
        free(*elf_header_data);
    }

    return status;
}

/**
 * Close the elf file, given the NFS file handle.
 * @param fh The NFS file handle of the elf
 * @returns 0 on success, -1 otherwise.
 */
static int close_elf(struct nfsfh* fh) {
    nfs_close_cb_args_t args = {
        .thread_index = current_thread->thread_id,
        .expected_pid = current_thread->assigned_pid
    };
    struct nfs_context *nfs_context = get_nfs_context();

    int ret = nfs_close_async(nfs_context, fh, nfs_close_cb, (void *)&args);
    if (ret < 0)
    {
        ZF_LOGE("Failed to queue nfs_close_async");
        return -1;
    }

    seL4_Wait(worker_threads[current_thread->thread_id]->ntfn, NULL);

    if (args.status < 0)
    {
        ZF_LOGE("Failed to close elf file handle on NFS");
        return -1;
    }
    return 0;
}

static int get_elf_stat(const char* path, sos_stat_t* elf_stat) {
    struct nfs_context *nfs_context = get_nfs_context();
    nfs_stat_cb_args_t private_data = {
        .thread_index = current_thread->thread_id,
        .expected_pid = current_thread->assigned_pid,
        .status = 0
    };

    int err = nfs_stat64_async(nfs_context, path, nfs_stat_cb, &private_data);
    if (err < 0)
    {
        ZF_LOGE("An error occured when trying to queue the command nfs_stat64_async. The callback will not be invoked.");
        return -1;
    }

    seL4_Wait(worker_threads[current_thread->thread_id]->ntfn, NULL);

    if (private_data.status == -1) {
        ZF_LOGE("Failed to read status of elf file on NFS");
        return -1;
    }

    *elf_stat = private_data.sos_stat;
    return 0;
}

static int stack_write(seL4_Word *mapped_stack, int index, uintptr_t val)
{
    mapped_stack[index] = val;
    return index - 1;
}
/** Extract the `__vsyscall` section offset, with respect to the elf base.
 * This function does not handle corrupted ELF file.
*/
static uintptr_t* get_vsyscall_offset_wrt_elf(elf_t *elf_file, struct nfsfh *elf_fh) {
    /* assume given elf is elf64 */

    /** Section table is an array of headers. We can extract section name offset (w.r.t string table) from it. 
        Now load section table to memory to easily access it. 
        The downsides of this is that we load everything in at once. However it makes our life easier by directly accessing them later.
    */
    size_t section_table_offset = (uintptr_t)elf64_getSectionTable(elf_file) - (uintptr_t)elf_file->elfFile;
    size_t num_sections = elf_getNumSections(elf_file);
    
    size_t section_headers_total_size = num_sections * sizeof(Elf64_Shdr);
    Elf64_Shdr *section_headers = malloc(section_headers_total_size);
    if (section_headers == NULL) {
        ZF_LOGE("Failed to allocate memory for section headers");
        return NULL;
    }
    nfs_pread_cb_args_t args = {
        .thread_index = current_thread->thread_id,
        .read_buf = section_headers,
        .expected_pid = current_thread->assigned_pid
    };

    if (nfs_pread_wrapper(elf_fh, &args, section_table_offset, section_headers_total_size)) {
        ZF_LOGE("Failed to read section table");
        free(section_headers);
        return NULL;
    }

    size_t str_table_idx = elf_getSectionStringTableIndex(elf_file);
    size_t str_table_offset = section_headers[str_table_idx].sh_offset; /* string table offset, relative to elf base */
   
    size_t __vsyscall_str_size = strlen("__vsyscall");
    char *section_name = malloc(__vsyscall_str_size + 1);
    section_name[__vsyscall_str_size] = '\0';

    for (size_t i = 0; i < num_sections; i++) {
        /* section name offset, with respect to string table*/
        size_t section_name_offset_wrt_str_table = section_headers[i].sh_name;

        /* section name offset, with respect to elf*/
        size_t section_name_offset_wrt_elf = str_table_offset + section_name_offset_wrt_str_table;

        /* read the section name */
        args = (nfs_pread_cb_args_t) {
            .thread_index = current_thread->thread_id,
            .read_buf = section_name,
            .expected_pid = current_thread->assigned_pid
        };
        if (nfs_pread_wrapper(elf_fh, &args, section_name_offset_wrt_elf, __vsyscall_str_size)) {
            ZF_LOGI("Failed to read section name at section number %d. Continue to read other section names.", i);
            continue;
        }

        if (strcmp(section_name, "__vsyscall") == 0) {
            free(section_headers);
            free(section_name);
            return section_headers[i].sh_offset; /* gets the offset only */
        }
    }

    free(section_headers);
    free(section_name);
    return NULL;
}
/* set up System V ABI compliant stack, so that the process can
 * start up and initialise the C library */
static uintptr_t init_process_stack(cspace_t *cspace, seL4_CPtr local_vspace, elf_t *elf_file, struct nfsfh *elf_fh, pid_t pid)
{
    user_process_t *user_process = user_processes[pid];
    /* virtual addresses in the target process' address space */
    uintptr_t stack_top = PROCESS_STACK_TOP;
    uintptr_t stack_bottom = PROCESS_STACK_TOP - PAGE_SIZE_4K;
    /* virtual addresses in the SOS's address space */
    void *local_stack_top  = (seL4_Word *) SOS_SCRATCH;
    uintptr_t local_stack_bottom = SOS_SCRATCH - PAGE_SIZE_4K;

    /* find the vsyscall table */
    uintptr_t sysinfo_section;
    if (elf_fh == NULL) {
        uintptr_t *sysinfo = (uintptr_t *) elf_getSectionNamed(elf_file, "__vsyscall", NULL);
        if (!sysinfo || !(*sysinfo)) {
            ZF_LOGE("syscall table ptr is NULL");
            return 0;
        }
        sysinfo_section = *sysinfo;
    } else { /* for elf originated from NFS, we've only loaded the header, hence we couldn't just read the content from *sysinfo. */
        uintptr_t vsyscall_offset = get_vsyscall_offset_wrt_elf(elf_file, elf_fh);
        nfs_pread_cb_args_t args = {
            .thread_index = current_thread->thread_id, 
            .read_buf = &sysinfo_section,
            .expected_pid = current_thread->assigned_pid
        };
        if (nfs_pread_wrapper(elf_fh, &args, vsyscall_offset, sizeof(sysinfo_section))) {
            return 0;
        }
    }

    /* allocate a stack frame for the user application*/
    seL4_Error err = alloc_map_frame(cspace, stack_bottom, user_process, seL4_ReadWrite);
    page_metadata_t *page = find_page(stack_bottom, user_process->page_global_directory);
    user_process->stack = page->frame_cap;

    /* allocate a slot to duplicate the stack frame cap so we can map it into our address space */
    seL4_CPtr local_stack_cptr = cspace_alloc_slot(cspace);
    if (local_stack_cptr == seL4_CapNull) {
        ZF_LOGE("Failed to alloc slot for stack");
        return 0;
    }

    /* copy the stack frame cap into the slot */
    err = cspace_copy(cspace, local_stack_cptr, cspace, user_process->stack, seL4_AllRights);
    if (err != seL4_NoError) {
        cspace_free_slot(cspace, local_stack_cptr);
        ZF_LOGE("Failed to copy cap");
        return 0;
    }

    /* map it into the sos address space */
    err = map_frame(cspace, local_stack_cptr, local_vspace, local_stack_bottom, seL4_AllRights,
                    seL4_ARM_Default_VMAttributes);
    if (err != seL4_NoError) {
        cspace_delete(cspace, local_stack_cptr);
        cspace_free_slot(cspace, local_stack_cptr);
        return 0;
    }

    int index = -2;

    /* null terminate the aux vectors */
    index = stack_write(local_stack_top, index, 0);
    index = stack_write(local_stack_top, index, 0);

    /* write the aux vectors */
    index = stack_write(local_stack_top, index, PAGE_SIZE_4K);
    index = stack_write(local_stack_top, index, AT_PAGESZ);

    index = stack_write(local_stack_top, index, sysinfo_section);
    index = stack_write(local_stack_top, index, AT_SYSINFO);

    index = stack_write(local_stack_top, index, PROCESS_IPC_BUFFER);
    index = stack_write(local_stack_top, index, AT_SEL4_IPC_BUFFER_PTR);

    /* null terminate the environment pointers */
    index = stack_write(local_stack_top, index, 0);

    /* we don't have any env pointers - skip */

    /* null terminate the argument pointers */
    index = stack_write(local_stack_top, index, 0);

    /* no argpointers - skip */

    /* set argc to 0 */
    stack_write(local_stack_top, index, 0);

    /* adjust the initial stack top */
    stack_top += (index * sizeof(seL4_Word));

    /* the stack *must* remain aligned to a double word boundary,
     * as GCC assumes this, and horrible bugs occur if this is wrong */
    assert(index % 2 == 0);
    assert(stack_top % (sizeof(seL4_Word) * 2) == 0);

    /* unmap our copy of the stack */
    err = seL4_ARM_Page_Unmap(local_stack_cptr);
    assert(err == seL4_NoError);

    /* delete the copy of the stack frame cap */
    err = cspace_delete(cspace, local_stack_cptr);
    assert(err == seL4_NoError);

    /* mark the slot as free */
    cspace_free_slot(cspace, local_stack_cptr);

    /* Exend the stack with extra pages */
    for (int page = 0; page < INITIAL_PROCESS_STACK_PAGES; page++) {
        stack_bottom -= PAGE_SIZE_4K;
        int result = alloc_map_frame(cspace, stack_bottom, user_process, seL4_ReadWrite);
        if (result != 0) {
            ZF_LOGE("Unable to allocate a new frame at %p!\n", (void*)stack_bottom);
            return 0;
        }
    }
    /* Create a stack region */
    user_process->stack_region = add_vm_region(user_process->vm_regions, stack_top, MAX_PROCESS_STACK_PAGES * PAGE_SIZE_4K, seL4_ReadWrite, true);
    if (user_process->stack_region == NULL) {
        ZF_LOGE("Unable to add stack region");
        return 0;
    }
    user_process->guard_page_vaddr = stack_bottom - PAGE_SIZE_4K;
    return stack_top;
}

bool create_process(sos_thread_t* assigned_worker_thread, char *app_name, pid_t pid, elf_t* elf_file, struct nfsfh* elf_fh) 
{
    user_processes[pid] = malloc(sizeof(user_process_t));    
    ZF_LOGF_IF(!user_processes[pid], "Failed to malloc user_process_t for pid=%u", pid);

    user_process_t *user_process = user_processes[pid];

    /* Create a VSpace */ 
    user_process->vspace_ut = alloc_retype(&user_process->vspace, seL4_ARM_PageGlobalDirectoryObject, seL4_PGDBits);
    if (user_process->vspace_ut == NULL) {
        return false;
    }

    /* assign the vspace to an asid pool */
    seL4_Word err = seL4_ARM_ASIDPool_Assign(seL4_CapInitThreadASIDPool, user_process->vspace);
    if (err != seL4_NoError) {
        ZF_LOGE("Failed to assign asid pool");
        return false;
    }

    /* Create a simple 1 level CSpace */
    err = cspace_create_one_level(&cspace, &user_process->cspace);
    if (err != CSPACE_NOERROR) {
        ZF_LOGE("Failed to create cspace");
        return false;
    }
    
    /* Initialise the virtual file system */
    if (init_vfs(&user_process->vfs) == -1) {
        ZF_LOGE("Failed to init vfs");
        return false;
    }

    /* Initialise the page global directory, for virtual memory management */
    user_process->page_global_directory = create_pgd();
    if (!user_process->page_global_directory) {
        ZF_LOGE("Failed to alloc page global directory");
        return false;
    }

    /* Initialise a linked list of vm_regions */
    if (init_vm_regions(&user_process->vm_regions) == -1) {
        ZF_LOGE("Failed to init vm regions");
        return false;
    }   

    /* Initialise a mmap red-black tree */ 
    init_mmap_region(&user_process->mmap_region);

    /* Init waitlist*/
    if (init_waitlist(&user_process->waitlist) == -1) {
        ZF_LOGE("Failed to init waitlist");
        return false;
    }

    /* Create an IPC buffer */
    err = alloc_map_frame(&cspace, PROCESS_IPC_BUFFER, user_process, seL4_AllRights);
    if (err != 0) {
        ZF_LOGE("Unable to map IPC buffer for user app");
        return false;
    }

    /* Keep track of IPC buffer region */
    vm_region_t *ipc_region = add_vm_region(user_process->vm_regions, PROCESS_IPC_BUFFER, PAGE_SIZE_4K, seL4_AllRights, false);
    if (ipc_region == NULL) {
        ZF_LOGE("Unable to add ipc region");
        return false;
    }

    /* Saves the IPC buffer capability */
    page_metadata_t *page_metadata = find_page(PROCESS_IPC_BUFFER, user_process->page_global_directory);
    user_process->ipc_buffer = page_metadata->frame_cap;

    /* allocate a new slot in the target cspace which we will mint a badged endpoint cap into --
     * the badge is used to identify the process, which will come in handy when you have multiple
     * processes. */

    user_process->user_ep = cspace_alloc_slot(&user_process->cspace);
    if (user_process->user_ep == seL4_CapNull) {
        ZF_LOGE("Failed to alloc user ep slot");
        return false;
    }

    /* now mutate the cap, thereby setting the badge */
    // TODO: consider if it is necessary to set the badge here.
    if (elf_fh) {
        err = cspace_mint(&user_process->cspace, user_process->user_ep, &cspace, assigned_worker_thread->ipc_ep, seL4_AllRights, 0);
    } else { /* elf_fh == NULL means this is loading the initial user process, hence set the badge */
        err = cspace_mint(&user_process->cspace, user_process->user_ep, &cspace, assigned_worker_thread->ipc_ep, seL4_AllRights, APP_EP_BADGE);
    }

    if (err) {
        ZF_LOGE("Failed to mint user ep");
        return false;
    }

    /* Create a new TCB object */
    user_process->tcb_ut = alloc_retype(&user_process->tcb, seL4_TCBObject, seL4_TCBBits);
    if (user_process->tcb_ut == NULL) {
        ZF_LOGE("Failed to alloc tcb ut");
        return false;
    }

    /* Configure the TCB */
    err = seL4_TCB_Configure(user_process->tcb, (user_process->cspace).root_cnode, seL4_NilData,
                             user_process->vspace, seL4_NilData, PROCESS_IPC_BUFFER,
                             user_process->ipc_buffer);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to configure new TCB");
        return false;
    }

    /* Create scheduling context */
    user_process->sched_context_ut = alloc_retype(&user_process->sched_context, seL4_SchedContextObject,
                                                     seL4_MinSchedContextBits);
    if (user_process->sched_context_ut == NULL) {
        ZF_LOGE("Failed to alloc sched context ut");
        return false;
    }

    /* Configure the scheduling context to use the first core with budget equal to period */
    err = seL4_SchedControl_Configure(sched_ctrl_start, user_process->sched_context, US_IN_MS, US_IN_MS, 0, 0);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to configure scheduling context");
        return false;
    }

    /* bind sched context, set fault endpoint and priority
     * In MCS, fault end point needed here should be in current thread's cspace.
     * NOTE this will use the unbadged ep unlike above, you might want to mint it with a badge
     * so you can identify which thread faulted in your fault handler */
    err = seL4_TCB_SetSchedParams(user_process->tcb, seL4_CapInitThreadTCB, seL4_MinPrio, APP_PRIORITY,
                                  user_process->sched_context, assigned_worker_thread->ipc_ep);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to set scheduling params, seL4_Error = %d\n", err);
        return false;
    }

    /* Provide a name for the thread -- Helpful for debugging */
    NAME_THREAD(user_process->tcb, app_name);

    /* set up the stack */
    seL4_Word sp = init_process_stack(&cspace, seL4_CapInitThreadVSpace, elf_file, elf_fh, pid);
    if (sp == NULL) {
        ZF_LOGE("Failed to init process stack");
        return false;
    }

    /* load the elf image */
    err = elf_load(&cspace, elf_file, elf_fh, user_process);
    if (err) {
        ZF_LOGE("Failed to load elf image");
        return false;
    }

    /* Start the new process */
    seL4_UserContext context = {
        .pc = elf_getEntryPoint(elf_file),
        .sp = sp,
    };

    strncpy(user_process->command, app_name, N_NAME);
    user_process->stime = get_time() / 1000;
    user_process->assigned_worker_thread_id = assigned_worker_thread->thread_id;

    printf("Starting %s at %p\n", app_name, (void *) context.pc);
    err = seL4_TCB_WriteRegisters(user_process->tcb, true, 0, sizeof(context) / sizeof(seL4_Word), &context);
    ZF_LOGE_IF(err, "Failed to write registers");

    return err == seL4_NoError;
}

int handle_sos_process_create() {
    ZF_LOGV("syscall: process_create!\n");

    uintptr_t path_vaddr = seL4_GetMR(1);
    size_t path_len = seL4_GetMR(2);

    char* path = NULL;

    // open to read
    struct nfsfh *elf_fh = open_elf(path_vaddr, path_len, &path);
    if (elf_fh == NULL) {
        ZF_LOGE("Failed to open elf file on NFS");

        free(path); /* path can either be freed already or not. Just free it again anyways. */
        return -1;
    }
    ZF_LOGE("finish open elf\n");

    sos_stat_t elf_stat;
    if (get_elf_stat(path, &elf_stat)) {
        ZF_LOGE("Failed to get elf stat");
        
        free(path);
        close_elf(elf_fh);
        return -1;
    }
    ZF_LOGE("finish get elf stat\n");

    if (!(elf_stat.st_fmode & FM_EXEC)) {
        ZF_LOGE("Elf file must be executable");

        free(path);
        close_elf(elf_fh);
        return -1;
    }

    // read one page of content. We assume that header size is less than the page size.
    unsigned char* elf_header_data = NULL; 
    if (read_elf_header(elf_fh, &elf_header_data)) {
        ZF_LOGE("Failed to read first page of data from elf on NFS");
        
        free(path);
        close_elf(elf_fh);
        return -1;
    }
    ZF_LOGE("finish read header \n");

    // make an elf_t out of it
    elf_t elf_file;
    if (elf_newFile((const void*) elf_header_data, elf_stat.st_size, &elf_file)) {
        ZF_LOGE("Invalid elf file");
        
        free(path);
        free(elf_header_data);
        close_elf(elf_fh);
        return -1;
    }

    // gets next pid
    pid_t pid = get_available_pid();
    if (pid == -1) {
        ZF_LOGE("Reached max num processes");

        free(path);
        free(elf_header_data);
        close_elf(elf_fh);
        return -1;
    }
    // assigns user process id to a thread
    sos_thread_t *worker_thread = get_available_worker_thread();
    if (worker_thread == NULL) {
        ZF_LOGE("No available worker thread");

        free(path);
        free(elf_header_data);
        close_elf(elf_fh);
        return -1;
    }

    if (!create_process(worker_thread, path, pid, &elf_file, elf_fh)) {
        ZF_LOGE("Failed to create a new process");

        free(path);
        free(elf_header_data);
        close_elf(elf_fh);

        // TODO: clean up the user process.....
        user_process_t *user_process = user_processes[pid];

        // vfs
        free(user_process->vfs);

        // pgd
        free(user_process->page_global_directory);
        destroy_pgd(user_process->page_global_directory, &cspace, user_process->vspace);
        // vm regions
        free(user_process->vm_regions);
        
        // waitlist
        free(user_process->waitlist->ntfns);
        free(user_process->waitlist);

        // tcb
        free_cap(user_process->tcb_ut, user_process->tcb);

        // sche dcontext
        free_cap(user_process->sched_context_ut, user_process->sched_context);

        // vspace
        free_cap(user_process->vspace_ut, user_process->vspace);
        
        // cspace
        cspace_destroy(&user_process->cspace);

        free(user_process);
        return -1;
    }

    worker_thread->assigned_pid = pid;

    return pid;
}