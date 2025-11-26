#pragma once
#include <sel4/sel4.h>
#include "ut.h"
typedef struct {
    seL4_CPtr notification;
    ut_t *notification_ut;
    volatile int value;
} sync_bin_sem_t;

typedef sync_bin_sem_t sync_mutex_t;

/* Initialise an unmanaged mutex with a notification object
 * @param sem           A mutex object to be initialised.
 * @param notification  A notification object to use for the lock.
 * @return              0 on success, an error code on failure. */
int sync_mutex_init(sync_mutex_t *mutex, seL4_CPtr notification);

/* Acquire a mutex
 * @param mutex         An initialised mutex to acquire.
 * @return              0 on success, an error code on failure. */
int sync_mutex_lock(sync_mutex_t *mutex);

/* Release a mutex
 * @param mutex         An initialised mutex to release.
 * @return              0 on success, an error code on failure. */
int sync_mutex_unlock(sync_mutex_t *mutex);

/* Allocate and initialise a managed mutex
 * @param mutex         A mutex object to initialise.
 * @return              0 on success, an error code on failure. */
int sync_mutex_new(sync_mutex_t *mutex);

/* Deallocate a managed mutex (do not use with sync_mutex_init)
 * @param mutex         A mutex object initialised by sync_mutex_new.
 * @return              0 on success, an error code on failure. */
int sync_mutex_destroy(sync_mutex_t *mutex);
