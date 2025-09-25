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
    struct sc_heap id_heap;
} clock;

struct id_heap_data {
    uint32_t id;
};

struct timeout_data {
    uint32_t id;
    timer_callback_t callback;
};

struct timeout_heap_data {
    // The time when the delay ends. Will be used as key to determine priority.
    timestamp_t timeout_time;

    struct timeout_data *timeout_data;
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

    // add the first id = 1 to id_heap
    uint32_t* id_ptr = malloc(sizeof(uint32_t));
    *id_ptr = 1;
    sc_heap_add(&clock.id_heap, 1, id_ptr);

    return CLOCK_R_OK;
}

timestamp_t get_time(void) {
    return read_timestamp(clock.regs);
}

uint32_t register_timer(uint64_t delay, timer_callback_t callback, void *data)
{   
    configure_timeout(clock.regs, MESON_TIMER_A, true, false, TIMEOUT_TIMEBASE_1_US, delay);
    // get the id from id-heap

    // push next id to id-heap

    // add id -> callback

    // push to min heap the (delay, id)
    return 1;
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
