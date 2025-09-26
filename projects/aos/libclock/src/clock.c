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
#include <utils/cset.h>
/* The functions in src/device.h should help you interact with the timer
 * to set registers and configure timeouts. */
#include "device.h"
Cset(uint32_t) cset;

static struct {
    volatile meson_timer_reg_t *regs;
    volatile bool has_started;
    /* Add fields as you see necessary */
    struct sc_heap timeout_heap;
    cset removed_ids;
    cset used_ids;
    uint32_t next_timer_id;
} clock;

struct timeout_data {
    uint32_t id;
    timestamp_t timeout_timestamp;
    uint16_t delay;
    timer_callback_t callback;
    void* data;
};

struct register_timer_data {
    uint64_t delay;
    timer_callback_t callback;
    void *data;
};


int start_timer(unsigned char *timer_vaddr)
{
    int err = stop_timer();
    if (err != 0) {
        return err;
    }

    clock.regs = (meson_timer_reg_t *)(timer_vaddr + TIMER_REG_START);
    
    // start the internal counter, assume the tick frequency is 1us
    configure_timestamp(clock.regs, TIMESTAMP_TIMEBASE_1_US);
    
    // allow timers to be registered
    clock.has_started = true;

    cset__init(&clock.removed_ids);
    cset__init(&clock.used_ids);

    // first timer id starts with 1
    clock.next_timer_id = 1;
    return CLOCK_R_OK;
}

timestamp_t get_time(void) {
    return read_timestamp(clock.regs);
}

// Returns true when exist a timeout task to configure. Returns false when no timeout task exists.
bool reconfigure_timer_to_next_earliest_timeout() {
    struct sc_heap_data *next_earliest_timeout;
    while ( next_earliest_timeout = sc_heap_peek(&clock.timeout_heap) ) {
        if (next_earliest_timeout == NULL) {
            return false;
        }
        
        struct timeout_data *next_earliest_timeout_data = next_earliest_timeout->data;

        bool removed = false;
        cset__contains(&clock.removed_ids, next_earliest_timeout_data->id , &removed);
        if (removed) {
            sc_heap_pop(&clock.timeout_heap);
            continue;
        }
        timestamp_t curr_time = get_time();
        uint16_t num_ticks = next_earliest_timeout_data->timeout_timestamp - curr_time;
        configure_timeout(clock.regs, MESON_TIMER_A, true, false, TIMEOUT_TIMEBASE_1_US, num_ticks);
        break;
    }

    return true;
}

void connecting_callback(uint32_t id, void *data) {
    register_timer(
        ((struct register_timer_data*)(data))->delay, 
        ((struct register_timer_data*)(data))->callback, 
        ((struct register_timer_data*)(data))->data
    );
}

// Delay time may exceed UINT16_T (which is what the hardware is capable of), 
// hence we breakdown the delay time in chunks of UINT16_T, connected by callbacks.
// Returns true on successful breakdown of delay, false when out of memory.
bool configure_timeout_data(
    struct timeout_data *timeout_data, 
    uint64_t delay, 
    timer_callback_t callback, 
    void *callback_data,
    uint32_t timer_id) 
{
    timeout_data->id = timer_id;
    if (delay > (uint64_t) UINT16_MAX) {
        timeout_data->callback = connecting_callback;
        timeout_data->timeout_timestamp = get_time() + (uint64_t) UINT16_MAX;
        timeout_data->delay = (uint64_t) UINT16_MAX;

        // construct callback data for connecting_callback()
        struct register_timer_data *register_timer_data = malloc(sizeof(struct register_timer_data));
        if (register_timer_data == NULL) {
            return false;
        }

        *register_timer_data = (struct register_timer_data) {
            .delay = delay - (uint64_t) UINT16_MAX,
            .callback = callback,
            .data = callback_data
        };

        timeout_data->data = register_timer_data;
    } else {
        timeout_data->delay = delay;
        timeout_data->callback = callback;
        timeout_data->data = callback_data;
        timeout_data->timeout_timestamp = get_time() + delay;
    }
    return true;
}

uint32_t register_timer(uint64_t delay, timer_callback_t callback, void *data)
{
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

    bool ret = configure_timeout_data(timeout_data, delay, callback, data, timer_id);
    if (!ret) return 0;

    ret = sc_heap_add(&clock.timeout_heap, timeout_data->timeout_timestamp, timeout_data);
    if (!ret) {
        return 0;
    }
    
    struct sc_heap_data *current_timeout = sc_heap_peek(&clock.timeout_heap);
    
    ret = reconfigure_timer_to_next_earliest_timeout();

    if (!ret) {
        return 0;
    }

    cset__add(&clock.used_ids, timer_id);
    return timer_id;
}

int remove_timer(uint32_t id)
{
    bool used = false;
    cset__contains(&clock.used_ids, id, &used);
    if (!used) {
        return CLOCK_R_FAIL;
    }
    // if the timeout is already being processed, then we pop it out of the heap and disable the timer
    struct sc_heap_data *next_earliest_timeout = sc_heap_peek(&clock.timeout_heap);
    if (next_earliest_timeout == NULL) {
        return CLOCK_R_FAIL;
    }
    struct timeout_data *timeout_data = next_earliest_timeout->data;
    if (timeout_data->id == id) {
        // disable the timeout timer
        configure_timeout(clock.regs, MESON_TIMER_A, false, false, TIMEOUT_TIMEBASE_1_MS, 0);
        sc_heap_pop(&clock.timeout_heap);
        reconfigure_timer_to_next_earliest_timeout();
    } else {
        cset__add(&clock.removed_ids, id);
    }

    cset__remove(&clock.used_ids, id);
    return CLOCK_R_OK;
}

int timer_irq(
    void *data,
    seL4_Word irq,
    seL4_IRQHandler irq_handler
)
{
    /* Handle the IRQ */
    timestamp_t current_timestamp = get_time();
    // pop from heap
    struct sc_heap_data *current_timeout = sc_heap_peek(&clock.timeout_heap);
    if (current_timeout == NULL) {
        return CLOCK_R_FAIL;
    }

    struct timeout_data *current_timeout_data = current_timeout->data;
    if (current_timeout_data->timeout_timestamp > current_timestamp) {
        return CLOCK_R_FAIL;
    }
    // invoke registered callback for all expired timeouts
    while ( (current_timeout = sc_heap_peek(&clock.timeout_heap)) && 
            current_timeout != NULL) {
        current_timeout_data = current_timeout->data;
        if (current_timeout_data->timeout_timestamp > current_timestamp) break;
        sc_heap_pop(&clock.timeout_heap);

        // skip removed timeout
        bool removed = false;
        cset__contains(&clock.removed_ids, current_timeout_data->id, &removed);
        if (removed) continue;

        current_timeout_data->callback(current_timeout_data->id, current_timeout_data->data);
    }

    reconfigure_timer_to_next_earliest_timeout();
    /* Acknowledge that the IRQ has been handled */
    seL4_IRQHandler_Ack(irq_handler);
    return CLOCK_R_OK;
}

int stop_timer(void)
{
    /* Stop the timer from producing further interrupts and remove all
     * existing timeouts */
    // return CLOCK_R_FAIL;
    return CLOCK_R_OK;
}
