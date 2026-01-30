#include "ipc.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>

// Union for semctl operations
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

int ipc_generate_keys(IPCKeys *keys, const char *path) {
    keys->shm_key = ftok(path, 'S');  // Shared memory
    keys->sem_key = ftok(path, 'E');  // Semaphores
    keys->mq_cashier_key = ftok(path, 'C');
    keys->mq_platform_key = ftok(path, 'P');
    keys->mq_boarding_key = ftok(path, 'B');
    keys->mq_arrivals_key = ftok(path, 'A');

    if (keys->shm_key == -1 || keys->sem_key == -1 ||
        keys->mq_cashier_key == -1 || keys->mq_platform_key == -1 ||
        keys->mq_boarding_key == -1 || keys->mq_arrivals_key == -1) {
        perror("ipc_generate_keys: ftok");
        return -1;
    }

    log_debug("IPC", "Generated IPC keys: shm=%d sem=%d mq_c=%d mq_p=%d mq_b=%d mq_a=%d",
              keys->shm_key, keys->sem_key,
              keys->mq_cashier_key, keys->mq_platform_key,
              keys->mq_boarding_key, keys->mq_arrivals_key);

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
    res->state = NULL;

    // Create shared memory
    res->shm_id = shmget(keys->shm_key, sizeof(SharedState), IPC_CREAT | IPC_EXCL | 0666);
    if (res->shm_id == -1) {
        perror("ipc_create: shmget");
        goto cleanup;
    }
    log_debug("IPC", "Created shared memory: id=%d", res->shm_id);

    // Attach shared memory
    res->state = (SharedState *)shmat(res->shm_id, NULL, 0);
    if (res->state == (void *)-1) {
        perror("ipc_create: shmat");
        res->state = NULL;
        goto cleanup;
    }
    log_debug("IPC", "Attached shared memory");

    // Initialize shared memory to zero
    memset(res->state, 0, sizeof(SharedState));

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
    sem_values[SEM_WORKER_READY] = 0;                   // Sync (deprecated)
    sem_values[SEM_PAUSE] = 0;                          // Pause sync
    sem_values[SEM_PLATFORM_GATES] = PLATFORM_GATES;    // 3 platform gates
    sem_values[SEM_LOWER_READY] = 0;                    // Lower worker ready (issue #1)
    sem_values[SEM_UPPER_READY] = 0;                    // Upper worker ready (issue #1)
    sem_values[SEM_EMERGENCY_CLEAR] = 0;                // Emergency clear (issue #4)

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

    // Copy config to shared state
    res->state->station_capacity = cfg->station_capacity;
    res->state->tourist_spawn_rate = cfg->tourist_spawn_rate;
    res->state->max_concurrent_tourists = cfg->max_concurrent_tourists;
    res->state->vip_percentage = cfg->vip_percentage;
    res->state->walker_percentage = cfg->walker_percentage;
    res->state->trail_walk_time = cfg->trail_walk_time;
    res->state->trail_bike_fast_time = cfg->trail_bike_fast_time;
    res->state->trail_bike_medium_time = cfg->trail_bike_medium_time;
    res->state->trail_bike_slow_time = cfg->trail_bike_slow_time;
    res->state->ticket_t1_duration = cfg->ticket_t1_duration;
    res->state->ticket_t2_duration = cfg->ticket_t2_duration;
    res->state->ticket_t3_duration = cfg->ticket_t3_duration;

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

    // Get shared memory
    res->shm_id = shmget(keys->shm_key, sizeof(SharedState), 0666);
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

    return 0;
}

void ipc_detach(IPCResources *res) {
    if (res->state) {
        shmdt(res->state);
        res->state = NULL;
    }
}

void ipc_destroy(IPCResources *res) {
    // Detach and remove shared memory
    if (res->state) {
        shmdt(res->state);
        res->state = NULL;
    }
    if (res->shm_id != -1) {
        if (shmctl(res->shm_id, IPC_RMID, NULL) == -1) {
            perror("ipc_destroy: shmctl IPC_RMID");
        } else {
            log_debug("IPC", "Removed shared memory");
        }
        res->shm_id = -1;
    }

    // Remove semaphores
    if (res->sem_id != -1) {
        if (semctl(res->sem_id, 0, IPC_RMID) == -1) {
            perror("ipc_destroy: semctl IPC_RMID");
        } else {
            log_debug("IPC", "Removed semaphore set");
        }
        res->sem_id = -1;
    }

    // Remove message queues
    if (res->mq_cashier_id != -1) {
        if (msgctl(res->mq_cashier_id, IPC_RMID, NULL) == -1) {
            perror("ipc_destroy: msgctl cashier IPC_RMID");
        } else {
            log_debug("IPC", "Removed cashier message queue");
        }
        res->mq_cashier_id = -1;
    }

    if (res->mq_platform_id != -1) {
        if (msgctl(res->mq_platform_id, IPC_RMID, NULL) == -1) {
            perror("ipc_destroy: msgctl platform IPC_RMID");
        } else {
            log_debug("IPC", "Removed platform message queue");
        }
        res->mq_platform_id = -1;
    }

    if (res->mq_boarding_id != -1) {
        if (msgctl(res->mq_boarding_id, IPC_RMID, NULL) == -1) {
            perror("ipc_destroy: msgctl boarding IPC_RMID");
        } else {
            log_debug("IPC", "Removed boarding message queue");
        }
        res->mq_boarding_id = -1;
    }

    if (res->mq_arrivals_id != -1) {
        if (msgctl(res->mq_arrivals_id, IPC_RMID, NULL) == -1) {
            perror("ipc_destroy: msgctl arrivals IPC_RMID");
        } else {
            log_debug("IPC", "Removed arrivals message queue");
        }
        res->mq_arrivals_id = -1;
    }

    log_info("IPC", "All IPC resources destroyed");
}

int sem_wait(int sem_id, int sem_num) {
    struct sembuf sop = {sem_num, -1, 0};

    while (semop(sem_id, &sop, 1) == -1) {
        if (errno == EINTR) {
            continue;  // Retry on interrupt
        }
        perror("sem_wait: semop");
        return -1;
    }
    return 0;
}

int sem_post(int sem_id, int sem_num) {
    struct sembuf sop = {sem_num, 1, 0};

    while (semop(sem_id, &sop, 1) == -1) {
        if (errno == EINTR) {
            continue;  // Retry on interrupt
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
    sem_wait(res->sem_id, SEM_STATE);
    int paused = res->state->paused;
    if (paused) {
        // Track that we're waiting
        res->state->pause_waiters++;
    }
    sem_post(res->sem_id, SEM_STATE);

    if (paused) {
        // Block until SIGCONT releases us
        sem_wait(res->sem_id, SEM_PAUSE);
    }
}

/**
 * Wait for emergency stop to clear.
 * Uses semaphore blocking instead of usleep polling (issue #4 fix).
 */
void ipc_wait_emergency_clear(IPCResources *res) {
    sem_wait(res->sem_id, SEM_STATE);
    int emergency = res->state->emergency_stop;
    if (emergency) {
        // Track that we're waiting
        res->state->emergency_waiters++;
    }
    sem_post(res->sem_id, SEM_STATE);

    if (emergency) {
        // Block until emergency clears
        sem_wait(res->sem_id, SEM_EMERGENCY_CLEAR);
    }
}

/**
 * Release all processes waiting for emergency to clear.
 * Called when emergency stop is cleared (after both workers ready).
 */
void ipc_release_emergency_waiters(IPCResources *res) {
    sem_wait(res->sem_id, SEM_STATE);
    int waiters = res->state->emergency_waiters;
    res->state->emergency_waiters = 0;
    sem_post(res->sem_id, SEM_STATE);

    // Release all waiters
    for (int i = 0; i < waiters; i++) {
        sem_post(res->sem_id, SEM_EMERGENCY_CLEAR);
    }

    if (waiters > 0) {
        log_debug("IPC", "Released %d emergency waiters", waiters);
    }
}

/**
 * Release all processes waiting for pause to end.
 * Called on SIGCONT in main process.
 */
void ipc_release_pause_waiters(IPCResources *res) {
    sem_wait(res->sem_id, SEM_STATE);
    int waiters = res->state->pause_waiters;
    res->state->pause_waiters = 0;
    sem_post(res->sem_id, SEM_STATE);

    // Release all waiters
    for (int i = 0; i < waiters; i++) {
        sem_post(res->sem_id, SEM_PAUSE);
    }

    if (waiters > 0) {
        log_debug("IPC", "Released %d pause waiters", waiters);
    }
}
