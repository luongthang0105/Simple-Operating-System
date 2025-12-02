#pragma once
#include <utils/sglib.h>

typedef struct mmap_tree {
    uintptr_t vaddr_base;
    size_t length;
    int prot;

    char color_field;
    struct mmap_tree *left;
    struct mmap_tree *right;
} mmap_tree;

#define MMAP_CMP(x,y) ((y->vaddr_base)-(x->vaddr_base))

SGLIB_DEFINE_RBTREE_PROTOTYPES(mmap_tree, left, right, color_field, MMAP_CMP);

long handle_sos_mmap();
void init_mmap_region(mmap_tree **mmap_tree);
