#include "fault_handler.h"
#include "../user_process.h"
#include <sel4/sel4_arch/types_gen.h>
#include <aos/debug.h>
#ifdef CONFIG_SOS_GDB_ENABLED
#include "debugger.h"
#endif /* CONFIG_SOS_GDB_ENABLED */

seL4_MessageInfo_t handle_fault(seL4_MessageInfo_t tag, bool *have_reply, seL4_CPtr worker_thread_ntfn, const char *thread_name) {
    seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
    int ret = -1; /* by default, the return value from fault handler equals -1, so we don't reply to fault that hasn't been handled */
    seL4_Fault_t fault = seL4_getFault(tag);
    seL4_Uint64 fault_type = seL4_Fault_get_seL4_FaultType(fault);

    switch (fault_type) {
        
        case seL4_Fault_VMFault:
            ret = handle_vm_fault(fault, worker_thread_ntfn);
            break;
        default:
            /* some kind of fault */
            debug_print_fault(tag, thread_name);
            /* dump registers too */
            debug_dump_registers(user_process.tcb);

            ZF_LOGE("Unknown fault %lu\n", fault_type);
            break;
    }

    if (ret == 0) {
        *have_reply = true;
        seL4_SetMR(0, 0);
    } else {
        *have_reply = false;
    }
    
    return reply_msg;
}
