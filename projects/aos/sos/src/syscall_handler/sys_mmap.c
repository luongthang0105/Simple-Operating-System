#include <stdio.h>
#include "../user_process.h"
#include "../pagetable.h"
#include <errno.h>

SGLIB_DEFINE_RBTREE_FUNCTIONS(mmap_tree, left, right, color_field, MMAP_CMP);

void init_mmap_region(mmap_tree **mmap_tree) {
    *mmap_tree = NULL;
    return;
}

long mmap_tree_insert_region(mmap_tree **mmap_tree, uintptr_t vaddr_base, size_t length, int prot) {
    struct mmap_tree *new_mmap_node = malloc(sizeof(struct mmap_tree));
    if (new_mmap_node == NULL) {
        ZF_LOGE("Failed to malloc memory for new mmap node\n");
        return -ENOMEM;
    }

    new_mmap_node->vaddr_base = vaddr_base;
    new_mmap_node->length = length;
    new_mmap_node->prot = prot;
    sglib_mmap_tree_add(mmap_tree, new_mmap_node);

    return vaddr_base;
}

long mmap_allocate_auto(size_t required_length, int prot, user_process_t *user_process) {
    struct mmap_tree *mmap_node;
    struct sglib_mmap_tree_iterator it;

    uintptr_t heap_end = user_process->heap_region->vaddr_base + user_process->heap_region->size;
    uintptr_t stack_top = user_process->guard_page_vaddr - 2 * PAGE_SIZE_4K;

    /* there is no allocated mmap node, we will find an allocation between stack top and heap end */
    if (sglib_mmap_tree_len(user_process->mmap_region) == 0) {
        if (stack_top - required_length >= heap_end) { /* found a big enough gap! */
            return mmap_tree_insert_region(&user_process->mmap_region, PAGE_ALIGN_4K(stack_top - required_length), required_length, prot);
        } else {
            return -ENOMEM; /* required length is too big */
        }
    }

    /* there must be at least one mmap node in the tree */
    uintptr_t prev_base = stack_top;
    
    for (mmap_node = sglib_mmap_tree_it_init_inorder(&it, user_process->mmap_region); mmap_node != NULL; mmap_node = sglib_mmap_tree_it_next(&it)) {
        uintptr_t next_end = mmap_node->vaddr_base + mmap_node->length;

        // check if gap between prev_end (low of previous) and next_base (high of next) fits allocation
        if (next_end - prev_base >= required_length) {
            return mmap_tree_insert_region(&user_process->mmap_region, PAGE_ALIGN_4K(next_end), required_length, prot);
        }
        prev_base = mmap_node->vaddr_base;
    }

    /* check if the allocation interferes with the heap */
    if (prev_base - heap_end >= required_length) {
        return mmap_tree_insert_region(&user_process->mmap_region, PAGE_ALIGN_4K(prev_base - required_length), required_length, prot);
    }
    
    return -ENOMEM; /* Could not find a big enough gap */
}


long handle_sos_mmap() {
    ZF_LOGV("syscall:mmap!\n");

    size_t length = seL4_GetMR(2);
    int prot = seL4_GetMR(3);

    /* length must be a multiple of page size */
    if ((length % PAGE_SIZE_4K) != 0) return -1;

    user_process_t *user_process = get_current_user_process();

    return mmap_allocate_auto(length, prot, user_process);
}