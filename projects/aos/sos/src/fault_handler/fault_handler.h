#pragma once
#include <sel4/shared_types_gen.h>
#include "stdbool.h"
#include "vm_fault.h"
seL4_MessageInfo_t handle_fault(seL4_MessageInfo_t tag, bool *have_reply, seL4_CPtr worker_thread_ntfn, const char *thread_name);