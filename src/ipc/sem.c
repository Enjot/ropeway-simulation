/**
 * @file ipc/sem.c
 * @brief System V semaphore operations.
 */

#include "ipc/ipc.h"
#include "core/logger.h"

#include <errno.h>
#include <stdio.h>
#include <sys/sem.h>

// Union for semctl operations
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *_buf;
};

int ipc_sem_create(IPCResources *res, key_t key, const Config *cfg) {
    res->sem_id = semget(key, SEM_COUNT, IPC_CREAT | IPC_EXCL | 0666);
    if (res->sem_id == -1) {
        perror("ipc_sem_create: semget");
        return -1;
    }
    log_debug("IPC", "Created semaphore set: id=%d", res->sem_id);

    // Initialize semaphores
    union semun arg;
    unsigned short sem_values[SEM_COUNT];

    sem_values[SEM_STATE] = 1;
    sem_values[SEM_STATS] = 1;
    sem_values[SEM_ENTRY_GATES] = ENTRY_GATES;
    sem_values[SEM_EXIT_GATES] = EXIT_GATES;
    sem_values[SEM_LOWER_STATION] = cfg->station_capacity;
    sem_values[SEM_CHAIRS] = MAX_CHAIRS_IN_TRANSIT;
    sem_values[SEM_WORKER_READY] = 0;  // Startup barrier
    sem_values[SEM_PLATFORM_GATES] = PLATFORM_GATES;
    sem_values[SEM_EMERGENCY_CLEAR] = 0;
    sem_values[SEM_EMERGENCY_LOCK] = 1;

    arg.array = sem_values;
    if (semctl(res->sem_id, 0, SETALL, arg) == -1) {
        perror("ipc_sem_create: semctl SETALL");
        return -1;
    }
    log_debug("IPC", "Initialized semaphores: station_capacity=%d, sem_count=%d",
              cfg->station_capacity, SEM_COUNT);

    return 0;
}

int ipc_sem_attach(IPCResources *res, key_t key) {
    res->sem_id = semget(key, SEM_COUNT, 0666);
    if (res->sem_id == -1) {
        perror("ipc_sem_attach: semget");
        return -1;
    }
    return 0;
}

void ipc_sem_destroy(IPCResources *res) {
    if (res->sem_id != -1) {
        if (semctl(res->sem_id, 0, IPC_RMID) == -1) {
            perror("ipc_sem_destroy: semctl IPC_RMID");
        }
        res->sem_id = -1;
    }
}

void ipc_sem_destroy_signal_safe(IPCResources *res) {
    if (res->sem_id != -1) {
        semctl(res->sem_id, 0, IPC_RMID);
        res->sem_id = -1;
    }
}

/**
 * Atomically wait (decrement) a semaphore by count.
 * Blocks until count slots are available, then acquires all at once.
 */
int sem_wait(int sem_id, int sem_num, int count) {
    if (count <= 0) return 0;
    struct sembuf sop = {sem_num, -count, 0};

    if (semop(sem_id, &sop, 1) == -1) {
        // EINTR: interrupted by signal
        // EIDRM: semaphore removed while blocked
        // EINVAL: semaphore already removed before call
        if (errno == EINTR || errno == EIDRM || errno == EINVAL) {
            return -1;  // Let caller check g_running / handle shutdown
        }
        perror("sem_wait: semop");
        return -1;
    }
    return 0;
}

/**
 * Semaphore wait with EINTR handling.
 * Kernel handles SIGTSTP/SIGCONT automatically (process gets suspended).
 * Only returns -1 on shutdown (EIDRM) or other errors.
 */
int sem_wait_pauseable(IPCResources *res, int sem_num, int count) {
    if (count <= 0) return 0;
    struct sembuf sop = {sem_num, -count, 0};

    while (semop(res->sem_id, &sop, 1) == -1) {
        // EIDRM: semaphore removed while blocked
        // EINVAL: semaphore already removed before call
        if (errno == EIDRM || errno == EINVAL) {
            return -1;  // Shutdown - IPC destroyed
        }
        if (errno == EINTR) {
            // Signal interrupted us - just retry
            // Kernel handles SIGTSTP/SIGCONT automatically
            continue;
        }
        perror("sem_wait_pauseable: semop");
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
        // EINTR: interrupted by signal
        // EIDRM: semaphore removed while blocked
        // EINVAL: semaphore already removed before call
        if (errno == EINTR || errno == EIDRM || errno == EINVAL) {
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
        // EINTR, EIDRM, EINVAL: signal, shutdown, or semaphore already removed
        if (errno == EINTR || errno == EIDRM || errno == EINVAL) {
            return -1;
        }
        perror("sem_trywait: semop");
        return -1;
    }
    return 0;
}

int sem_getval(int sem_id, int sem_num) {
    int val = semctl(sem_id, sem_num, GETVAL);
    if (val == -1 && errno != EINVAL && errno != EIDRM) {
        perror("sem_getval: semctl GETVAL");
    }
    return val;
}
