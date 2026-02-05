/**
 * @file ipc/shm.c
 * @brief System V shared memory operations.
 */

#include "ipc/ipc.h"
#include "core/logger.h"

#include <stdio.h>
#include <string.h>
#include <sys/shm.h>

/**
 * @brief Create and attach shared memory segment.
 *
 * @param res IPC resources structure to populate.
 * @param key Shared memory key.
 * @param size Size of shared memory segment.
 * @return 0 on success, -1 on error.
 */
int ipc_shm_create(IPCResources *res, key_t key, size_t size) {
    res->shm_id = shmget(key, size, IPC_CREAT | IPC_EXCL | 0600);
    if (res->shm_id == -1) {
        perror("ipc_shm_create: shmget");
        return -1;
    }
    log_debug("IPC", "Created shared memory: id=%d, size=%zu", res->shm_id, size);

    // Attach shared memory
    res->state = (SharedState *)shmat(res->shm_id, NULL, 0);
    if (res->state == (void *)-1) {
        perror("ipc_shm_create: shmat");
        res->state = NULL;
        return -1;
    }
    log_debug("IPC", "Attached shared memory");

    // Initialize shared memory to zero (including flexible array)
    memset(res->state, 0, size);

    return 0;
}

/**
 * @brief Attach to existing shared memory segment.
 *
 * @param res IPC resources structure to populate.
 * @param key Shared memory key for lookup.
 * @return 0 on success, -1 on error.
 */
int ipc_shm_attach(IPCResources *res, key_t key) {
    // Get shared memory (size 0 to attach to existing segment with unknown size)
    res->shm_id = shmget(key, 0, 0600);
    if (res->shm_id == -1) {
        perror("ipc_shm_attach: shmget");
        return -1;
    }

    // Attach shared memory
    res->state = (SharedState *)shmat(res->shm_id, NULL, 0);
    if (res->state == (void *)-1) {
        perror("ipc_shm_attach: shmat");
        res->state = NULL;
        return -1;
    }

    return 0;
}

/**
 * @brief Detach from shared memory segment.
 *
 * @param res IPC resources to detach from.
 */
void ipc_shm_detach(IPCResources *res) {
    if (res->state) {
        shmdt(res->state);
        res->state = NULL;
    }
}

/**
 * @brief Detach and destroy shared memory segment.
 *
 * @param res IPC resources to destroy.
 */
void ipc_shm_destroy(IPCResources *res) {
    if (res->state) {
        shmdt(res->state);
        res->state = NULL;
    }
    if (res->shm_id != -1) {
        if (shmctl(res->shm_id, IPC_RMID, NULL) == -1) {
            perror("ipc_shm_destroy: shmctl IPC_RMID");
        }
        res->shm_id = -1;
    }
}

/**
 * @brief Signal-safe shared memory destruction.
 *
 * Uses only shmctl which is async-signal-safe.
 *
 * @param res IPC resources to destroy.
 */
void ipc_shm_destroy_signal_safe(IPCResources *res) {
    if (res->shm_id != -1) {
        shmctl(res->shm_id, IPC_RMID, NULL);
        res->shm_id = -1;
    }
}

/**
 * @brief Initialize shared state with configuration values.
 *
 * @param res IPC resources containing shared state.
 * @param cfg Configuration values to copy.
 */
void ipc_shm_init_state(IPCResources *res, const Config *cfg) {
    // Copy config to shared state
    res->state->station_capacity = cfg->station_capacity;
    res->state->tourists_to_generate = cfg->total_tourists;
    res->state->tourist_spawn_delay_us = cfg->tourist_spawn_delay_us;
    res->state->max_tracked_tourists = cfg->total_tourists;
    res->state->tourist_entry_count = 0;
    res->state->vip_percentage = cfg->vip_percentage;
    res->state->walker_percentage = cfg->walker_percentage;
    res->state->family_percentage = cfg->family_percentage;
    res->state->trail_walk_time = cfg->trail_walk_time;
    res->state->trail_bike_fast_time = cfg->trail_bike_fast_time;
    res->state->trail_bike_medium_time = cfg->trail_bike_medium_time;
    res->state->trail_bike_slow_time = cfg->trail_bike_slow_time;
    res->state->ticket_t1_duration = cfg->ticket_t1_duration;
    res->state->ticket_t2_duration = cfg->ticket_t2_duration;
    res->state->ticket_t3_duration = cfg->ticket_t3_duration;
    res->state->danger_probability = cfg->danger_probability;
    res->state->danger_duration_sim = cfg->danger_duration_sim;
    res->state->debug_logs_enabled = cfg->debug_logs_enabled;
    res->state->scared_enabled = cfg->scared_enabled;

    // Set initial state
    res->state->running = 1;
    res->state->closing = 0;
    res->state->emergency_stop = 0;
}
