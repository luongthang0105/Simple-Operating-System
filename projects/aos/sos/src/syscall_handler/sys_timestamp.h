#pragma once
#include <stdint.h>
#include <sel4/shared_types_gen.h>
#include <aos/sel4_zf_logif.h>
#include <clock/clock.h>

/**
 * @brief Handles the sos_timestamp syscall for a user process.
 *
 * Returns the current system timestamp.
 *
 * @return The current timestamp.
 */
timestamp_t handle_sos_timestamp();