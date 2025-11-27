#pragma once
#include "sys_open.h"
#include "sys_close.h"
#include "sys_read.h"
#include "sys_write.h"
#include "sys_stat.h"
#include "sys_brk.h"
#include "sys_getdirent.h"
#include "sys_usleep.h"
#include "sys_timestamp.h"
#include "sys_process_create.h"
#include "sys_process_delete.h"
#include "sys_process_wait.h"
#include "sys_process_status.h"
#include "sys_my_id.h"
#include <utils/attribute.h>
#include <sel4/functions.h>
/**
 * Deals with a syscall and sets the message registers before returning the
 * message info to be passed through to seL4_ReplyRecv()
 */
seL4_MessageInfo_t handle_syscall(UNUSED seL4_Word badge, UNUSED int num_args, bool *have_reply);
