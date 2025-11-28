#include "waitlist.h"
#include <sossharedapi/syscalls.h>

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

int add_waiter(waitlist_t *waitlist, seL4_CPtr ipc_ep) {
    seL4_CPtr *ipc_ep_ptr = malloc(sizeof(seL4_CPtr));
    *ipc_ep_ptr = ipc_ep;

    list_prepend(waitlist->ntfns, (void*) ipc_ep_ptr);

    return 0;
}

int signal_then_destroy_caps(waitlist_t *waitlist) {
    assert(waitlist->ntfns != NULL);

    for (struct list_node *cur = waitlist->ntfns->head; cur != NULL;) {
        seL4_CPtr *ipc_ep_ptr = cur->data;

        seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_Fault_NullFault, 0, 0, 1);
        seL4_SetMR(0, SYSCALL_NULL_OPS);
        seL4_NBSend(*ipc_ep_ptr, tag);

        // let the owner thread of these ntfn decides if they want to free the ntfn

        free(ipc_ep_ptr);
        
        struct list_node *next = cur->next;
        free(cur);
        cur = next;
    }

    return 0;
}
