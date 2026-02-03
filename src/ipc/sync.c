/**
 * @file ipc/sync.c
 * @brief Emergency and worker barrier synchronization.
 */

#include "ipc/ipc.h"
#include "core/logger.h"

#include <stdio.h>

/**
 * Wait for emergency stop to clear.
 * Uses semaphore blocking instead of usleep polling.
 */
void ipc_wait_emergency_clear(IPCResources *res) {
    if (sem_wait(res->sem_id, SEM_STATE, 1) == -1) {
        return;  // Shutdown in progress
    }
    int emergency = res->state->emergency_stop;
    if (emergency) {
        // Track that we're waiting
        res->state->emergency_waiters++;
    }
    sem_post(res->sem_id, SEM_STATE, 1);

    if (emergency) {
        // Block until emergency clears
        sem_wait(res->sem_id, SEM_EMERGENCY_CLEAR, 1);  // May fail on shutdown, OK
    }
}

/**
 * Release all processes waiting for emergency to clear.
 * Called when emergency stop is cleared (after both workers ready).
 */
void ipc_release_emergency_waiters(IPCResources *res) {
    if (sem_wait(res->sem_id, SEM_STATE, 1) == -1) {
        return;  // Shutdown in progress
    }
    int waiters = res->state->emergency_waiters;
    res->state->emergency_waiters = 0;
    sem_post(res->sem_id, SEM_STATE, 1);

    // Release all waiters
    if (waiters > 0) {
        sem_post(res->sem_id, SEM_EMERGENCY_CLEAR, waiters);
        log_debug("IPC", "Released %d emergency waiters", waiters);
    }
}

/**
 * Signal that a worker has completed initialization and is ready.
 * Called by each worker (TimeServer, Cashier, LowerWorker, UpperWorker).
 */
void ipc_signal_worker_ready(IPCResources *res) {
    if (sem_post(res->sem_id, SEM_WORKER_READY, 1) == -1) {
        perror("ipc_signal_worker_ready: sem_post");
    }
}

/**
 * Wait for all workers to signal ready.
 * Called by main process before spawning tourist generator.
 *
 * @param res IPC resources
 * @param expected_count Number of workers to wait for (WORKER_COUNT_FOR_BARRIER)
 * @return 0 on success, -1 on error/timeout
 */
int ipc_wait_workers_ready(IPCResources *res, int expected_count) {
    log_debug("IPC", "Waiting for %d workers to be ready...", expected_count);

    // Wait for expected_count posts to SEM_WORKER_READY
    // Each worker posts 1, so we wait for the total count
    if (sem_wait(res->sem_id, SEM_WORKER_READY, expected_count) == -1) {
        perror("ipc_wait_workers_ready: sem_wait");
        return -1;
    }

    log_debug("IPC", "All %d workers are ready", expected_count);
    return 0;
}
