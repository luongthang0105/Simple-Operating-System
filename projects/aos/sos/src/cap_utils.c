#include "cap_utils.h"

extern cspace_t cspace;

ut_t *create_cap(seL4_CPtr *cap, seL4_ObjectType type, int bits) {
    ut_t *ut = alloc_retype(cap, type, bits);
    ZF_LOGF_IF(!ut, "No memory for notification object");
    return ut;
}

void free_cap(ut_t *ut, seL4_CPtr cap) {
    seL4_Error del_error = cspace_delete(&cspace, cap);
    if (del_error != seL4_NoError) {
        ZF_LOGF("Failed to delete ntfn cap, seL4_Error = %d", del_error);
    }
    cspace_free_slot(&cspace, cap);
    ut_free(ut);
}