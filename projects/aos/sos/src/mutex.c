/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <autoconf.h>
#include <assert.h>
#include <sel4/sel4.h>
#include "utils.h"
#include "mutex.h"
#include <stddef.h>
#include "ut.h"
#include <cspace/cspace.h>

/** \brief Atomically increment an integer, accounting for possible overflow.
 *
 * @param x Pointer to integer to increment.
 * @param[out] oldval Previous value of the integer. May be written to even if
 *   the increment fails.
 * @param success_memorder The memory order to enforce
 * @return 0 if the increment succeeds, non-zero if it would cause an overflow.
 */
static inline int sync_atomic_increment_safe(volatile int *x, int *oldval, int success_memorder) {
    assert(x != NULL);
    assert(oldval != NULL);
    do {
        *oldval = *x;
        if (*oldval == INT_MAX) {
            /* We would overflow */
            return -1;
        }
    } while (!__atomic_compare_exchange_n(x, oldval, *oldval + 1, 1, success_memorder, __ATOMIC_RELAXED));
    return 0;
}

/** \brief Atomically decrement an integer, accounting for possible overflow.
 *
 * @param x Pointer to integer to decrement.
 * @param[out] oldval Previous value of the integer. May be written to even if
 *   the decrement fails.
 * @param success_memorder The memory order to enforce if the decrement is successful
 * @return 0 if the decrement succeeds, non-zero if it would cause an overflow.
 */
static inline int sync_atomic_decrement_safe(volatile int *x, int *oldval, int success_memorder) {
    assert(x != NULL);
    assert(oldval != NULL);
    do {
        *oldval = *x;
        if (*oldval == INT_MIN) {
            /* We would overflow */
            return -1;
        }
    } while (!__atomic_compare_exchange_n(x, oldval, *oldval - 1, 1, success_memorder, __ATOMIC_RELAXED));
    return 0;
}

/* Atomically increment an integer and return its new value. */
static inline int sync_atomic_increment(volatile int *x, int memorder) {
    return __atomic_add_fetch(x, 1, memorder);
}

/* Atomically decrement an integer and return its new value. */
static inline int sync_atomic_decrement(volatile int *x, int memorder) {
    return __atomic_sub_fetch(x, 1, memorder);
}

static inline int sync_bin_sem_bare_wait(seL4_CPtr notification, volatile int *value) {
    int oldval;
    int result = sync_atomic_decrement_safe(value, &oldval, __ATOMIC_ACQUIRE);
    if (result != 0) {
        /* Failed decrement; too many outstanding lock holders. */
        return -1;
    }
    if (oldval <= 0) {
        seL4_Wait(notification, NULL);
        /* Even though we performed an acquire barrier during the atomic
         * decrement we did not actually have the lock yet, so we have
         * to do another one now */
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
    }
    return 0;
}

static inline int sync_bin_sem_bare_post(seL4_CPtr notification, volatile int *value) {
    /* We can do an "unsafe" increment here because we know we are the only
     * lock holder.
     */
    int val = sync_atomic_increment(value, __ATOMIC_RELEASE);
    assert(*value <= 1);
    if (val <= 0) {
        seL4_Signal(notification);
    }
    return 0;
}


/* Initialise an unmanaged binary semaphore with a notification object
 * @param sem           A semaphore object to be initialised.
 * @param notification  A notification object to use for the lock.
 * @param value         The initial value for the semaphore. Must be 0 or 1.
 * @return              0 on success, an error code on failure. */
static inline int sync_bin_sem_init(sync_bin_sem_t *sem, seL4_CPtr notification, int value)
{
    if (sem == NULL) {
        ZF_LOGE("Semaphore passed to sync_bin_sem_init was NULL");
        return -1;
    }

    if (value != 0 && value != 1) {
        ZF_LOGE("Binary semaphore initial value neither 0 nor 1");
        return -1;
    }

    sem->notification = notification;
    sem->value = value;
    return 0;
}

/* Wait on a binary semaphore
 * @param sem           An initialised semaphore to acquire.
 * @return              0 on success, an error code on failure. */
static inline int sync_bin_sem_wait(sync_bin_sem_t *sem)
{
    if (sem == NULL) {
        ZF_LOGE("Semaphore passed to sync_bin_sem_wait was NULL");
        return -1;
    }
    return sync_bin_sem_bare_wait(sem->notification, &sem->value);
}

/* Signal a binary semaphore
 * @param sem           An initialised semaphore to release.
 * @return              0 on success, an error code on failure. */
static inline int sync_bin_sem_post(sync_bin_sem_t *sem)
{
    if (sem == NULL) {
        ZF_LOGE("Semaphore passed to sync_bin_sem_post was NULL");
        return -1;
    }
    return sync_bin_sem_bare_post(sem->notification, &sem->value);
}

/* Allocate and initialise a managed binary semaphore
 * @param sem           A semaphore object to initialise.
 * @param value         The initial value for the semaphore. Must be 0 or 1.
 * @return              0 on success, an error code on failure. */
static inline int sync_bin_sem_new(sync_bin_sem_t *sem, int value)
{
    if (sem == NULL) {
        ZF_LOGE("Semaphore passed to sync_bin_sem_new was NULL");
        return -1;
    }
    if (value != 0 && value != 1) {
        ZF_LOGE("Binary semaphore initial value neither 0 nor 1");
        return -1;
    }
    ut_t *ut = alloc_retype(&sem->notification, seL4_NotificationObject, seL4_NotificationBits);

    if (!ut) {
        return -1;
    } else {
        sem->notification_ut = ut;
        return sync_bin_sem_init(sem, sem->notification, value);
    }
}

/* Deallocate a managed binary semaphore (do not use with sync_bin_sem_init)
 * @param sem           A semaphore object initialised by sync_bin_sem_new.
 * @return              0 on success, an error code on failure. */
static inline int sync_bin_sem_destroy(sync_bin_sem_t *sem)
{
    if (sem == NULL) {
        ZF_LOGE("Semaphore passed to sync_bin_sem_destroy was NULL");
        return -1;
    }

    cspace_delete(&cspace, sem->notification);
    cspace_free_slot(&cspace, sem->notification);
    ut_free(sem->notification_ut);
    return 0;
}


/* Initialise an unmanaged mutex with a notification object
 * @param sem           A mutex object to be initialised.
 * @param notification  A notification object to use for the lock.
 * @return              0 on success, an error code on failure. */
int sync_mutex_init(sync_mutex_t *mutex, seL4_CPtr notification) {
    return sync_bin_sem_init(mutex, notification, 1);
}

/* Acquire a mutex
 * @param mutex         An initialised mutex to acquire.
 * @return              0 on success, an error code on failure. */
int sync_mutex_lock(sync_mutex_t *mutex) {
    return sync_bin_sem_wait(mutex);
}

/* Release a mutex
 * @param mutex         An initialised mutex to release.
 * @return              0 on success, an error code on failure. */
int sync_mutex_unlock(sync_mutex_t *mutex) {
    return sync_bin_sem_post(mutex);
}

/* Allocate and initialise a managed mutex
 * @param mutex         A mutex object to initialise.
 * @return              0 on success, an error code on failure. */
int sync_mutex_new(sync_mutex_t *mutex) {
    return sync_bin_sem_new(mutex, 1);
}

/* Deallocate a managed mutex (do not use with sync_mutex_init)
 * @param mutex         A mutex object initialised by sync_mutex_new.
 * @return              0 on success, an error code on failure. */
int sync_mutex_destroy(sync_mutex_t *mutex) {
    return sync_bin_sem_destroy(mutex);
}

