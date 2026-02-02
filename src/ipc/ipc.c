/**
 * @file ipc/ipc.c
 * @brief IPC orchestration - coordinates creation and destruction of all IPC resources.
 */

#include "ipc/ipc.h"
#include "ipc/internal.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

void ipc_detach(IPCResources *res) {
    ipc_shm_detach(res);
}

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
 * Signal-safe IPC cleanup for use in signal handlers.
 * Only uses async-signal-safe syscalls (msgctl, semctl, shmctl).
 * Cleanup order: MQ -> Semaphores -> SHM (unblocks waiting processes first).
 */
void ipc_cleanup_signal_safe(IPCResources *res) {
    // Clean up message queues first (unblocks msgrcv/msgsnd)
    ipc_mq_destroy_signal_safe(res);

    // Clean up semaphores (unblocks semop)
    ipc_sem_destroy_signal_safe(res);

    // Clean up shared memory
    ipc_shm_destroy_signal_safe(res);
}
