#pragma once

#include <utils/list.h>
#include "recursive_mutex.h"

typedef struct waitlist {
    list_t *ntfns;
} waitlist_t;

extern cspace_t cspace;

int init_waitlist(waitlist_t **out_waitlist);

/**
 * Add a notification cap to the choosen user process's waitlist.
 * 
 * It is the caller responsibility to make sure when `add_waiter` is called, 
 * the respective user process (owner of `waitlist`) is not being destructed.
 * 
 * If this is not guaranteed, then the `waitlist` param will be freed and not usable.
 * Also, calls on `waitlist->mutex` will result in undefined behaviour because it is destroyed as well.
 * 
 * @returns 0 on success, -1 otherwise.
 */
int add_waiter(waitlist_t *waitlist, seL4_CPtr src_ntfn);

/** Signals all the ntfns currently waiting on the user process to exit.
 *  It also destroy the waitlist, hence later calls to `add_waiter` is denied.
*/
int signal_then_destroy_caps(waitlist_t *waitlist);
