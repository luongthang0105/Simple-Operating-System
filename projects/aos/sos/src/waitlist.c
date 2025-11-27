#include "waitlist.h"

int init_waitlist(waitlist_t **waitlist_out) {
    waitlist_t *waitlist = malloc(sizeof(waitlist_t));
    if (!waitlist) {
        ZF_LOGE("Failed to alloc waitlist");
        return -1;
    }
    
    waitlist->ntfns = malloc(sizeof(list_t));
    if (!waitlist) {
        ZF_LOGE("Failed to alloc ntfns list");
        free(waitlist);
        return -1;
    }
    list_init(waitlist->ntfns);

    *waitlist_out = waitlist;
    return 0;
}

int add_waiter(waitlist_t *waitlist, seL4_CPtr src_ntfn) {
    seL4_CPtr *ntfn_ptr = malloc(sizeof(seL4_CPtr));
    *ntfn_ptr = src_ntfn;

    list_prepend(waitlist->ntfns, (void*) ntfn_ptr);

    return 0;
}

int signal_then_destroy_caps(waitlist_t *waitlist) {
    assert(waitlist->ntfns != NULL);

    for (struct list_node *cur = waitlist->ntfns->head; cur != NULL;) {
        seL4_CPtr *ntfn_ptr = cur->data;
        seL4_Signal(*ntfn_ptr);

        // let the owner thread of these ntfn free the ntfns themselves

        free(ntfn_ptr);
        
        struct list_node *next = cur->next;
        free(cur);
        cur = next;
    }

    return 0;
}
