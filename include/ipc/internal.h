#pragma once

/**
 * @file ipc/internal.h
 * @brief Internal IPC functions (not part of public API).
 *
 * These functions are used by ipc.c to orchestrate resource creation/destruction.
 * They should not be called directly by application code.
 */

#include "ipc/resources.h"
#include "config.h"

// ============================================================================
// Shared Memory (shm.c)
// ============================================================================

int ipc_shm_create(IPCResources *res, key_t key, size_t size);
int ipc_shm_attach(IPCResources *res, key_t key);
void ipc_shm_detach(IPCResources *res);
void ipc_shm_destroy(IPCResources *res);
void ipc_shm_destroy_signal_safe(IPCResources *res);
void ipc_shm_init_state(IPCResources *res, const Config *cfg);

// ============================================================================
// Semaphores (sem.c)
// ============================================================================

int ipc_sem_create(IPCResources *res, key_t key, const Config *cfg);
int ipc_sem_attach(IPCResources *res, key_t key);
void ipc_sem_destroy(IPCResources *res);
void ipc_sem_destroy_signal_safe(IPCResources *res);

// ============================================================================
// Message Queues (mq.c)
// ============================================================================

int ipc_mq_create(IPCResources *res, const IPCKeys *keys);
int ipc_mq_attach(IPCResources *res, const IPCKeys *keys);
void ipc_mq_destroy(IPCResources *res);
void ipc_mq_destroy_signal_safe(IPCResources *res);
