/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <clock/clock.h>
#include <utils/sc_heap.h>

/* The functions in src/device.h should help you interact with the timer
 * to set registers and configure timeouts. */
#include "device.h"

static struct {
    volatile meson_timer_reg_t *regs;
    volatile bool has_started;
    /* Add fields as you see necessary */
    struct sc_heap timeout_heap;
    uint32_t next_timer_id;
} clock;

struct timeout_data {
    uint32_t id;
    timestamp_t timeout_timestamp;
    timer_callback_t callback;
    void* data;
};

int start_timer(unsigned char *timer_vaddr)
{
    int err = stop_timer();
    if (err != 0) {
        return err;
    }

    clock.regs = (meson_timer_reg_t *)(timer_vaddr + TIMER_REG_START);
    
    // start the internal counter, assume the tick frequency is in microseconds
    configure_timestamp(clock.regs, TIMESTAMP_TIMEBASE_1_US);
    
    // allow timers to be registered
    clock.has_started = true;

    // first timer id starts with 1
    clock.next_timer_id = 1;
    return CLOCK_R_OK;
}

timestamp_t get_time(void) {
    return read_timestamp(clock.regs);
}

uint32_t register_timer(uint64_t delay, timer_callback_t callback, void *data)
{   
    // configure_timeout(clock.regs, MESON_TIMER_A, true, false, TIMEOUT_TIMEBASE_1_US, delay);
    // get the id for this timer
    uint32_t timer_id = clock.next_timer_id;
   
    // update to next id
    if (clock.next_timer_id == UINT32_MAX) { // when id is currently UINT32_MAX, next id must be 1, due to `register_timer()` return value requirements (returns 0 on failure)
        clock.next_timer_id = 1;
    } else {
        clock.next_timer_id += 1;
    }

    // push to min heap the (delay, id)
    struct timeout_data *timeout_data = malloc(sizeof(struct timeout_data));
    if (timeout_data == NULL) {
        return 0;
    }

    timeout_data->callback = callback;
    timeout_data->id = timer_id;
    timeout_data->data = data;
    timeout_data->timeout_timestamp = get_time() + delay;

    bool ret = sc_heap_add(&clock.timeout_heap, timeout_data->timeout_timestamp, timeout_data);
    if (!ret) {
        return 0;
    }

    ret = reconfigure_timer_to_next_earliest_timeout();

    if (!ret) {
        return 0;
    }

    return timer_id;
}

bool reconfigure_timer_to_next_earliest_timeout() {
    struct timeout_data *next_earliest_timeout_data = sc_heap_peek(&clock.timeout_heap);
    if (next_earliest_timeout_data == NULL) {
        return false;
    }
    
    timestamp_t next_earliest_timeout = next_earliest_timeout_data->timeout_timestamp;
    uint16_t num_ticks = next_earliest_timeout - get_time();
    configure_timeout(clock.regs, MESON_TIMER_A, true, false, TIMEOUT_TIMEBASE_1_US, num_ticks);

    return true;
}
int remove_timer(uint32_t id)
{
    // remove id -> callback entry
    // call configure_timeout with enable = false
    // add id back to the id-heap
    return CLOCK_R_FAIL;
}

int timer_irq(
    void *data,
    seL4_Word irq,
    seL4_IRQHandler irq_handler
)
{
    /* Handle the IRQ */
    /* Acknowledge that the IRQ has been handled */
    seL4_IRQHandler_Ack(irq_handler);
    return CLOCK_R_FAIL;
}

int stop_timer(void)
{
    /* Stop the timer from producing further interrupts and remove all
     * existing timeouts */
    // return CLOCK_R_FAIL;
    return CLOCK_R_OK;
}
