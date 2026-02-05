#pragma once

/**
 * @file ipc/ipc.h
 * @brief IPC resource management and semaphore operations.
 */

#include "ipc/resources.h"
#include "core/config.h"

/**
 * @brief Generate IPC keys using ftok.
 *
 * @param keys Structure to populate with generated keys.
 * @param path File path to use for key generation.
 * @return 0 on success, -1 on error.
 */
int ipc_generate_keys(IPCKeys *keys, const char *path);

/**
 * @brief Clean up stale IPC resources from a previous crashed run.
 *
 * Checks if shared memory exists and if the main_pid stored in it is dead.
 *
 * @param keys IPC keys to check for stale resources.
 * @return 1 if stale resources cleaned, 0 if no stale resources, -1 on error.
 */
int ipc_cleanup_stale(const IPCKeys *keys);

/**
 * @brief Create all IPC resources (shared memory, semaphores, message queues).
 *
 * Should only be called by main process.
 *
 * @param res Structure to populate with created resource IDs.
 * @param keys IPC keys for resource creation.
 * @param cfg Configuration for initial values.
 * @return 0 on success, -1 on error.
 */
int ipc_create(IPCResources *res, const IPCKeys *keys, const Config *cfg);

/**
 * @brief Attach to existing IPC resources (for child processes).
 *
 * @param res Structure to populate with attached resource IDs.
 * @param keys IPC keys for resource lookup.
 * @return 0 on success, -1 on error.
 */
int ipc_attach(IPCResources *res, const IPCKeys *keys);

/**
 * @brief Detach from IPC resources (for child processes before exit).
 *
 * @param res IPC resources to detach from.
 */
void ipc_detach(IPCResources *res);

/**
 * @brief Destroy all IPC resources (for main process cleanup).
 *
 * @param res IPC resources to destroy.
 */
void ipc_destroy(IPCResources *res);

/**
 * @brief Signal-safe IPC cleanup (for use in signal handlers).
 *
 * Only uses async-signal-safe syscalls (msgctl, semctl, shmctl).
 * No logging, no memory allocation.
 *
 * @param res IPC resources to clean up.
 */
void ipc_cleanup_signal_safe(IPCResources *res);

/**
 * @brief Atomically wait (decrement) a semaphore by count.
 *
 * Blocks until count slots are available, then acquires all at once.
 * Handles EINTR by returning -1 (caller should check shutdown flag).
 *
 * @param sem_id Semaphore set ID.
 * @param sem_num Semaphore index within the set.
 * @param count Number of slots to acquire.
 * @return 0 on success, -1 on error or signal interruption.
 */
int sem_wait(int sem_id, int sem_num, int count);

/**
 * @brief Atomically post (increment) a semaphore by count.
 *
 * Releases count slots at once.
 *
 * @param sem_id Semaphore set ID.
 * @param sem_num Semaphore index within the set.
 * @param count Number of slots to release.
 * @return 0 on success, -1 on error.
 */
int sem_post(int sem_id, int sem_num, int count);

/**
 * @brief Non-blocking semaphore wait (try to decrement by 1).
 *
 * @param sem_id Semaphore set ID.
 * @param sem_num Semaphore index within the set.
 * @return 0 on success, -1 if would block (EAGAIN) or on error.
 */
int sem_trywait(int sem_id, int sem_num);

/**
 * @brief Semaphore wait with EINTR handling and pause support.
 *
 * Kernel handles SIGTSTP/SIGCONT automatically (process gets suspended).
 * Retries on EINTR, only returns -1 on shutdown (EIDRM) or other errors.
 *
 * @param res IPC resources containing semaphore ID.
 * @param sem_num Semaphore index within the set.
 * @param count Number of slots to acquire.
 * @return 0 on success, -1 on shutdown or error.
 */
int sem_wait_pauseable(IPCResources *res, int sem_num, int count);

/**
 * @brief Get current semaphore value.
 *
 * @param sem_id Semaphore set ID.
 * @param sem_num Semaphore index within the set.
 * @return Current value, or -1 on error.
 */
int sem_getval(int sem_id, int sem_num);

/**
 * @brief Wait for emergency stop to clear.
 *
 * Properly tracks emergency_waiters for reliable wakeup.
 *
 * @param res IPC resources.
 */
void ipc_wait_emergency_clear(IPCResources *res);

/**
 * @brief Release all emergency waiters (called when emergency clears).
 *
 * @param res IPC resources.
 */
void ipc_release_emergency_waiters(IPCResources *res);

/**
 * @brief Signal worker ready for startup synchronization barrier.
 *
 * Workers call this after initialization to signal readiness.
 *
 * @param res IPC resources.
 * @return 0 on success, -1 on error.
 */
int ipc_signal_worker_ready(IPCResources *res);

/**
 * @brief Wait for all workers to be ready.
 *
 * Main process calls this before spawning tourist generator.
 *
 * @param res IPC resources.
 * @param expected_count Number of workers to wait for.
 * @return 0 on success, -1 on error.
 */
int ipc_wait_workers_ready(IPCResources *res, int expected_count);
