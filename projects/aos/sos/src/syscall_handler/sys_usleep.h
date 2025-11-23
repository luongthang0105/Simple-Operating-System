#pragma once
#include <stdint.h>
#include <sel4/shared_types_gen.h>
#include <aos/sel4_zf_logif.h>

/**
 * @brief Handles the sos_usleep syscall for a user process.
 *
 * Registers a timer to block the current thread for the specified number
 * of milliseconds, then waits until the timer expires.
 *
 * @return 0 on completion.
 */

int handle_sos_usleep();