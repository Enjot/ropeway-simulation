#include "ipc.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>

// Union for semctl operations
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *_buf;
};

int ipc_generate_keys(IPCKeys *keys, const char *path) {
    keys->shm_key = ftok(path, 'S');  // Shared memory
    keys->sem_key = ftok(path, 'E');  // Semaphores
    keys->mq_cashier_key = ftok(path, 'C');
    keys->mq_platform_key = ftok(path, 'P');
    keys->mq_boarding_key = ftok(path, 'B');
    keys->mq_arrivals_key = ftok(path, 'A');
    keys->mq_worker_key = ftok(path, 'W');

    if (keys->shm_key == -1 || keys->sem_key == -1 ||
        keys->mq_cashier_key == -1 || keys->mq_platform_key == -1 ||
        keys->mq_boarding_key == -1 || keys->mq_arrivals_key == -1 ||
        keys->mq_worker_key == -1) {
        perror("ipc_generate_keys: ftok");
        return -1;
    }

    log_debug("IPC", "Generated IPC keys: shm=%d sem=%d mq_c=%d mq_p=%d mq_b=%d mq_a=%d mq_w=%d",
              keys->shm_key, keys->sem_key,
              keys->mq_cashier_key, keys->mq_platform_key,
              keys->mq_boarding_key, keys->mq_arrivals_key, keys->mq_worker_key);

    return 0;
}

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

    // Calculate shared memory size (base + flexible array for tourist entries)
    size_t shm_size = sizeof(SharedState) + (cfg->max_tracked_tourists * sizeof(TouristEntry));

    // Create shared memory
    res->shm_id = shmget(keys->shm_key, shm_size, IPC_CREAT | IPC_EXCL | 0666);
    if (res->shm_id == -1) {
        perror("ipc_create: shmget");
        goto cleanup;
    }
    log_debug("IPC", "Created shared memory: id=%d, size=%zu", res->shm_id, shm_size);

    // Attach shared memory
    res->state = (SharedState *)shmat(res->shm_id, NULL, 0);
    if (res->state == (void *)-1) {
        perror("ipc_create: shmat");
        res->state = NULL;
        goto cleanup;
    }
    log_debug("IPC", "Attached shared memory");

    // Initialize shared memory to zero (including flexible array)
    memset(res->state, 0, shm_size);

    // Create semaphore set
    res->sem_id = semget(keys->sem_key, SEM_COUNT, IPC_CREAT | IPC_EXCL | 0666);
    if (res->sem_id == -1) {
        perror("ipc_create: semget");
        goto cleanup;
    }
    log_debug("IPC", "Created semaphore set: id=%d", res->sem_id);

    // Initialize semaphores
    union semun arg;
    unsigned short sem_values[SEM_COUNT];

    sem_values[SEM_STATE] = 1;                          // Mutex
    sem_values[SEM_STATS] = 1;                          // Mutex
    sem_values[SEM_ENTRY_GATES] = ENTRY_GATES;          // 4 gates
    sem_values[SEM_EXIT_GATES] = EXIT_GATES;            // 2 gates
    sem_values[SEM_LOWER_STATION] = cfg->station_capacity;
    sem_values[SEM_CHAIRS] = MAX_CHAIRS_IN_TRANSIT;     // 36
    sem_values[SEM_WORKER_READY] = 0;                   // (deprecated/unused)
    sem_values[SEM_PAUSE] = 0;                          // Pause sync
    sem_values[SEM_PLATFORM_GATES] = PLATFORM_GATES;    // 3 platform gates
    sem_values[SEM_EMERGENCY_CLEAR] = 0;                // Emergency clear (tourist waiters)

    arg.array = sem_values;
    if (semctl(res->sem_id, 0, SETALL, arg) == -1) {
        perror("ipc_create: semctl SETALL");
        goto cleanup;
    }
    log_debug("IPC", "Initialized semaphores: station_capacity=%d, sem_count=%d",
              cfg->station_capacity, SEM_COUNT);

    // Create message queues
    res->mq_cashier_id = msgget(keys->mq_cashier_key, IPC_CREAT | IPC_EXCL | 0666);
    if (res->mq_cashier_id == -1) {
        perror("ipc_create: msgget cashier");
        goto cleanup;
    }
    log_debug("IPC", "Created cashier message queue: id=%d", res->mq_cashier_id);

    res->mq_platform_id = msgget(keys->mq_platform_key, IPC_CREAT | IPC_EXCL | 0666);
    if (res->mq_platform_id == -1) {
        perror("ipc_create: msgget platform");
        goto cleanup;
    }
    log_debug("IPC", "Created platform message queue: id=%d", res->mq_platform_id);

    res->mq_boarding_id = msgget(keys->mq_boarding_key, IPC_CREAT | IPC_EXCL | 0666);
    if (res->mq_boarding_id == -1) {
        perror("ipc_create: msgget boarding");
        goto cleanup;
    }
    log_debug("IPC", "Created boarding message queue: id=%d", res->mq_boarding_id);

    res->mq_arrivals_id = msgget(keys->mq_arrivals_key, IPC_CREAT | IPC_EXCL | 0666);
    if (res->mq_arrivals_id == -1) {
        perror("ipc_create: msgget arrivals");
        goto cleanup;
    }
    log_debug("IPC", "Created arrivals message queue: id=%d", res->mq_arrivals_id);

    res->mq_worker_id = msgget(keys->mq_worker_key, IPC_CREAT | IPC_EXCL | 0666);
    if (res->mq_worker_id == -1) {
        perror("ipc_create: msgget worker");
        goto cleanup;
    }
    log_debug("IPC", "Created worker message queue: id=%d", res->mq_worker_id);

    // Copy config to shared state
    res->state->station_capacity = cfg->station_capacity;
    res->state->tourists_to_generate = cfg->total_tourists;
    res->state->tourist_spawn_delay_us = cfg->tourist_spawn_delay_us;
    res->state->max_tracked_tourists = cfg->max_tracked_tourists;
    res->state->tourist_entry_count = 0;
    res->state->vip_percentage = cfg->vip_percentage;
    res->state->walker_percentage = cfg->walker_percentage;
    res->state->trail_walk_time = cfg->trail_walk_time;
    res->state->trail_bike_fast_time = cfg->trail_bike_fast_time;
    res->state->trail_bike_medium_time = cfg->trail_bike_medium_time;
    res->state->trail_bike_slow_time = cfg->trail_bike_slow_time;
    res->state->ticket_t1_duration = cfg->ticket_t1_duration;
    res->state->ticket_t2_duration = cfg->ticket_t2_duration;
    res->state->ticket_t3_duration = cfg->ticket_t3_duration;
    res->state->danger_probability = cfg->danger_probability;
    res->state->danger_cooldown_sim = cfg->danger_cooldown_sim;

    // Set initial state
    res->state->running = 1;
    res->state->closing = 0;
    res->state->emergency_stop = 0;
    res->state->paused = 0;

    log_info("IPC", "All IPC resources created successfully");
    return 0;

cleanup:
    ipc_destroy(res);
    return -1;
}

int ipc_attach(IPCResources *res, const IPCKeys *keys) {
    memset(res, 0, sizeof(IPCResources));
    res->state = NULL;

    // Get shared memory (size 0 to attach to existing segment with unknown size)
    res->shm_id = shmget(keys->shm_key, 0, 0666);
    if (res->shm_id == -1) {
        perror("ipc_attach: shmget");
        return -1;
    }

    // Attach shared memory
    res->state = (SharedState *)shmat(res->shm_id, NULL, 0);
    if (res->state == (void *)-1) {
        perror("ipc_attach: shmat");
        res->state = NULL;
        return -1;
    }

    // Get semaphore set
    res->sem_id = semget(keys->sem_key, SEM_COUNT, 0666);
    if (res->sem_id == -1) {
        perror("ipc_attach: semget");
        return -1;
    }

    // Get message queues
    res->mq_cashier_id = msgget(keys->mq_cashier_key, 0666);
    if (res->mq_cashier_id == -1) {
        perror("ipc_attach: msgget cashier");
        return -1;
    }

    res->mq_platform_id = msgget(keys->mq_platform_key, 0666);
    if (res->mq_platform_id == -1) {
        perror("ipc_attach: msgget platform");
        return -1;
    }

    res->mq_boarding_id = msgget(keys->mq_boarding_key, 0666);
    if (res->mq_boarding_id == -1) {
        perror("ipc_attach: msgget boarding");
        return -1;
    }

    res->mq_arrivals_id = msgget(keys->mq_arrivals_key, 0666);
    if (res->mq_arrivals_id == -1) {
        perror("ipc_attach: msgget arrivals");
        return -1;
    }

    res->mq_worker_id = msgget(keys->mq_worker_key, 0666);
    if (res->mq_worker_id == -1) {
        perror("ipc_attach: msgget worker");
        return -1;
    }

    return 0;
}

void ipc_detach(IPCResources *res) {
    if (res->state) {
        shmdt(res->state);
        res->state = NULL;
    }
}

void ipc_destroy(IPCResources *res) {
    // Remove message queues first (before shared memory, so we can log)
    if (res->mq_cashier_id != -1) {
        if (msgctl(res->mq_cashier_id, IPC_RMID, NULL) == -1) {
            perror("ipc_destroy: msgctl cashier IPC_RMID");
        }
        res->mq_cashier_id = -1;
    }

    if (res->mq_platform_id != -1) {
        if (msgctl(res->mq_platform_id, IPC_RMID, NULL) == -1) {
            perror("ipc_destroy: msgctl platform IPC_RMID");
        }
        res->mq_platform_id = -1;
    }

    if (res->mq_boarding_id != -1) {
        if (msgctl(res->mq_boarding_id, IPC_RMID, NULL) == -1) {
            perror("ipc_destroy: msgctl boarding IPC_RMID");
        }
        res->mq_boarding_id = -1;
    }

    if (res->mq_arrivals_id != -1) {
        if (msgctl(res->mq_arrivals_id, IPC_RMID, NULL) == -1) {
            perror("ipc_destroy: msgctl arrivals IPC_RMID");
        }
        res->mq_arrivals_id = -1;
    }

    if (res->mq_worker_id != -1) {
        if (msgctl(res->mq_worker_id, IPC_RMID, NULL) == -1) {
            perror("ipc_destroy: msgctl worker IPC_RMID");
        }
        res->mq_worker_id = -1;
    }

    // Remove semaphores
    if (res->sem_id != -1) {
        if (semctl(res->sem_id, 0, IPC_RMID) == -1) {
            perror("ipc_destroy: semctl IPC_RMID");
        }
        res->sem_id = -1;
    }

    // Detach and remove shared memory last
    if (res->state) {
        shmdt(res->state);
        res->state = NULL;
    }
    if (res->shm_id != -1) {
        if (shmctl(res->shm_id, IPC_RMID, NULL) == -1) {
            perror("ipc_destroy: shmctl IPC_RMID");
        }
        res->shm_id = -1;
    }

    // Use write() directly since logger requires shared state
    write(STDERR_FILENO, "[INFO] [IPC] All IPC resources destroyed\n", 41);
}

/**
 * Atomically wait (decrement) a semaphore by count.
 * Blocks until count slots are available, then acquires all at once.
 */
int sem_wait(int sem_id, int sem_num, int count) {
    if (count <= 0) return 0;
    struct sembuf sop = {sem_num, -count, 0};

    if (semop(sem_id, &sop, 1) == -1) {
        if (errno == EINTR || errno == EIDRM) {
            return -1;  // Let caller check g_running / handle shutdown
        }
        perror("sem_wait: semop");
        return -1;
    }
    return 0;
}

/**
 * Atomically post (increment) a semaphore by count.
 * Releases count slots at once.
 */
int sem_post(int sem_id, int sem_num, int count) {
    if (count <= 0) return 0;
    struct sembuf sop = {sem_num, count, 0};

    if (semop(sem_id, &sop, 1) == -1) {
        if (errno == EINTR || errno == EIDRM) {
            return -1;  // Let caller check g_running / handle shutdown
        }
        perror("sem_post: semop");
        return -1;
    }
    return 0;
}

int sem_trywait(int sem_id, int sem_num) {
    struct sembuf sop = {sem_num, -1, IPC_NOWAIT};

    if (semop(sem_id, &sop, 1) == -1) {
        if (errno == EAGAIN) {
            return -1;  // Would block
        }
        if (errno == EINTR) {
            return -1;  // Interrupted
        }
        perror("sem_trywait: semop");
        return -1;
    }
    return 0;
}

int sem_getval(int sem_id, int sem_num) {
    int val = semctl(sem_id, sem_num, GETVAL);
    if (val == -1) {
        perror("sem_getval: semctl GETVAL");
    }
    return val;
}

/**
 * Check pause state and block if paused.
 * Properly tracks pause_waiters for reliable wakeup (issue #7, #11 fix).
 */
void ipc_check_pause(IPCResources *res) {
    if (sem_wait(res->sem_id, SEM_STATE, 1) == -1) {
        return;  // Shutdown in progress
    }
    int paused = res->state->paused;
    if (paused) {
        // Track that we're waiting
        res->state->pause_waiters++;
    }
    sem_post(res->sem_id, SEM_STATE, 1);

    if (paused) {
        // Block until SIGCONT releases us
        sem_wait(res->sem_id, SEM_PAUSE, 1);  // May fail on shutdown, OK
    }
}

/**
 * Wait for emergency stop to clear.
 * Uses semaphore blocking instead of usleep polling (issue #4 fix).
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
 * Release all processes waiting for pause to end.
 * Called on SIGCONT in main process.
 */
void ipc_release_pause_waiters(IPCResources *res) {
    if (sem_wait(res->sem_id, SEM_STATE, 1) == -1) {
        return;  // Shutdown in progress
    }
    int waiters = res->state->pause_waiters;
    res->state->pause_waiters = 0;
    sem_post(res->sem_id, SEM_STATE, 1);

    // Release all waiters
    if (waiters > 0) {
        sem_post(res->sem_id, SEM_PAUSE, waiters);
        log_debug("IPC", "Released %d pause waiters", waiters);
    }
}
