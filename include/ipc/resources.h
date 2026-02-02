#pragma once

/**
 * @file ipc/resources.h
 * @brief IPC resource management structures (keys and IDs).
 */

#include "ipc/shared_state.h"

// ============================================================================
// IPC Keys Structure
// ============================================================================

/**
 * @brief Container for all System V IPC keys.
 */
typedef struct {
    key_t shm_key;
    key_t sem_key;
    key_t mq_cashier_key;
    key_t mq_platform_key;
    key_t mq_boarding_key;
    key_t mq_arrivals_key;
    key_t mq_worker_key;
} IPCKeys;

// ============================================================================
// IPC IDs Structure
// ============================================================================

/**
 * @brief Container for all System V IPC resource IDs and attached shared memory.
 */
typedef struct {
    int shm_id;
    int sem_id;
    int mq_cashier_id;
    int mq_platform_id;
    int mq_boarding_id;
    int mq_arrivals_id;
    int mq_worker_id;
    SharedState *state;  // Attached shared memory pointer
} IPCResources;
