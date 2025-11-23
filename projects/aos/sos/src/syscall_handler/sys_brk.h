#pragma once
#include <sel4/shared_types_gen.h>
#include <aos/sel4_zf_logif.h>

/**
 * @brief Handles the sos_brk syscall, adjusting the process heap break.
 *
 * Returns the current program break if the argument is 0, or attempts to move
 * the break to `new_brk` by allocating or freeing pages as needed. The new
 * break must lie within the valid heap range and below the guard page. On
 * success, updates the heap size and returns the new break; otherwise returns 0.
 *
 * @return The updated program break on success, or 0 on failure.
 */
uintptr_t handle_sos_brk();