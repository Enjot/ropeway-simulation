#pragma once

/**
 * Shared emergency stop/resume functionality for lower and upper workers.
 * Eliminates code duplication between the two worker implementations.
 */

#include "constants.h"
#include "ipc/ipc.h"

/**
 * Worker emergency state - each worker maintains its own instance.
 * Passed to emergency functions so they can access/modify state.
 */
typedef struct {
    int *is_initiator;           // Pointer to worker's g_is_emergency_initiator
    double *start_time_sim;      // Pointer to worker's g_emergency_start_time_sim
} WorkerEmergencyState;

/**
 * Trigger an emergency stop.
 * Called when THIS worker detects danger.
 * Sets emergency flag, sends SIGUSR1 to other worker, records start time.
 *
 * @param res IPC resources
 * @param role This worker's role (WORKER_LOWER or WORKER_UPPER)
 * @param state Worker's emergency state pointers
 */
void worker_trigger_emergency_stop(IPCResources *res, WorkerRole role, WorkerEmergencyState *state);

/**
 * Acknowledge an emergency stop from the other worker.
 * Called when receiving SIGUSR1 signal.
 * Blocks until detecting worker initiates resume via message queue.
 *
 * @param res IPC resources
 * @param role This worker's role (WORKER_LOWER or WORKER_UPPER)
 * @param state Worker's emergency state pointers
 */
void worker_acknowledge_emergency_stop(IPCResources *res, WorkerRole role, WorkerEmergencyState *state);

/**
 * Initiate resume after cooldown period.
 * Called by detecting worker after cooldown to wake up the receiving worker.
 * Uses message queue handshake for synchronization.
 *
 * @param res IPC resources
 * @param role This worker's role (WORKER_LOWER or WORKER_UPPER)
 * @param state Worker's emergency state pointers
 */
void worker_initiate_resume(IPCResources *res, WorkerRole role, WorkerEmergencyState *state);
