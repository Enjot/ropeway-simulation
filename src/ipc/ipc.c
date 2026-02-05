/**
 * @file ipc/ipc.c
 * @brief IPC orchestration - coordinates creation and destruction of all IPC resources.
 */

#include "ipc/ipc.h"
#include "ipc/internal.h"
#include "core/logger.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>

/**
 * @brief Clean up stale IPC resources from a previous crashed run.
 *
 * Checks if shared memory exists and if the stored main_pid is dead.
 *
 * @param keys IPC keys to check for stale resources.
 * @return 1 if stale resources cleaned, 0 if no stale resources, -1 on error.
 */
int ipc_cleanup_stale(const IPCKeys *keys) {
    // Try to access existing shared memory (without IPC_CREAT)
    int shm_id = shmget(keys->shm_key, 0, 0600);
    if (shm_id == -1) {
        if (errno == ENOENT) {
            return 0;  // No existing shared memory - nothing to clean
        }
        // Other errors (permission, etc.) - report but don't fail
        perror("ipc_cleanup_stale: shmget check");
        return 0;
    }

    // Shared memory exists - attach and check if main process is alive
    SharedState *state = (SharedState *)shmat(shm_id, NULL, SHM_RDONLY);
    if (state == (void *)-1) {
        perror("ipc_cleanup_stale: shmat");
        return 0;
    }

    pid_t main_pid = state->main_pid;
    shmdt(state);

    // Check if main process is still alive (kill with signal 0 checks existence)
    if (main_pid > 0 && kill(main_pid, 0) == 0) {
        // Process still exists - resources are in use
        log_debug("IPC", "Found active simulation (PID %d), not cleaning", main_pid);
        return 0;
    }

    // Main process is dead - clean up orphaned resources
    write(STDERR_FILENO, "[INFO] [IPC] Cleaning stale IPC resources from previous run\n", 60);

    // Remove message queues
    int mq_ids[] = {
        msgget(keys->mq_cashier_key, 0600),
        msgget(keys->mq_platform_key, 0600),
        msgget(keys->mq_boarding_key, 0600),
        msgget(keys->mq_arrivals_key, 0600),
        msgget(keys->mq_worker_key, 0600)
    };
    for (int i = 0; i < 5; i++) {
        if (mq_ids[i] != -1) {
            msgctl(mq_ids[i], IPC_RMID, NULL);
        }
    }

    // Remove semaphores
    int sem_id = semget(keys->sem_key, 0, 0600);
    if (sem_id != -1) {
        semctl(sem_id, 0, IPC_RMID);
    }

    // Remove shared memory
    shmctl(shm_id, IPC_RMID, NULL);

    return 1;
}

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
int ipc_create(IPCResources *res, const IPCKeys *keys, const Config *cfg) {
    memset(res, 0, sizeof(IPCResources));
    res->shm_id = -1;
    res->sem_id = -1;
    res->mq_cashier_id = -1;
    res->mq_platform_id = -1;
    res->mq_boarding_id = -1;
    res->mq_arrivals_id = -1;
    res->mq_worker_id = -1;
    res->state = NULL;

    log_debug("IPC", "Generated IPC keys: shm=%d sem=%d mq_c=%d mq_p=%d mq_b=%d mq_a=%d mq_w=%d",
              keys->shm_key, keys->sem_key,
              keys->mq_cashier_key, keys->mq_platform_key,
              keys->mq_boarding_key, keys->mq_arrivals_key, keys->mq_worker_key);

    // Calculate shared memory size (base + flexible array for tourist entries)
    size_t shm_size = sizeof(SharedState) + (cfg->total_tourists * sizeof(TouristEntry));

    // Create shared memory
    if (ipc_shm_create(res, keys->shm_key, shm_size) == -1) {
        goto cleanup;
    }

    // Create semaphore set
    if (ipc_sem_create(res, keys->sem_key, cfg) == -1) {
        goto cleanup;
    }

    // Create message queues
    if (ipc_mq_create(res, keys) == -1) {
        goto cleanup;
    }

    // Initialize shared state with config values
    ipc_shm_init_state(res, cfg);

    log_debug("IPC", "All IPC resources created successfully");
    return 0;

cleanup:
    ipc_destroy(res);
    return -1;
}

/**
 * @brief Attach to existing IPC resources (for child processes).
 *
 * @param res Structure to populate with attached resource IDs.
 * @param keys IPC keys for resource lookup.
 * @return 0 on success, -1 on error.
 */
int ipc_attach(IPCResources *res, const IPCKeys *keys) {
    memset(res, 0, sizeof(IPCResources));
    res->state = NULL;

    // Attach to shared memory
    if (ipc_shm_attach(res, keys->shm_key) == -1) {
        return -1;
    }

    // Attach to semaphore set
    if (ipc_sem_attach(res, keys->sem_key) == -1) {
        return -1;
    }

    // Attach to message queues
    if (ipc_mq_attach(res, keys) == -1) {
        return -1;
    }

    return 0;
}

/**
 * @brief Detach from IPC resources (for child processes before exit).
 *
 * @param res IPC resources to detach from.
 */
void ipc_detach(IPCResources *res) {
    ipc_shm_detach(res);
}

/**
 * @brief Destroy all IPC resources (for main process cleanup).
 *
 * @param res IPC resources to destroy.
 */
void ipc_destroy(IPCResources *res) {
    // Remove message queues first (before shared memory, so we can log)
    ipc_mq_destroy(res);

    // Remove semaphores
    ipc_sem_destroy(res);

    // Detach and remove shared memory last
    ipc_shm_destroy(res);

    // Use write() directly since logger requires shared state
    write(STDERR_FILENO, "[INFO] [IPC] All IPC resources destroyed\n", 41);
}

/**
 * @brief Signal-safe IPC cleanup for use in signal handlers.
 *
 * Only uses async-signal-safe syscalls (msgctl, semctl, shmctl).
 * Cleanup order: MQ -> Semaphores -> SHM (unblocks waiting processes first).
 *
 * @param res IPC resources to clean up.
 */
void ipc_cleanup_signal_safe(IPCResources *res) {
    // Clean up message queues first (unblocks msgrcv/msgsnd)
    ipc_mq_destroy_signal_safe(res);

    // Clean up semaphores (unblocks semop)
    ipc_sem_destroy_signal_safe(res);

    // Clean up shared memory
    ipc_shm_destroy_signal_safe(res);
}
