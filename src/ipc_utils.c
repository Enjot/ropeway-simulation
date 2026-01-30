#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>

#include "ipc_utils.h"
#include "config.h"
#include "logger.h"

/* Generate IPC key for shared memory */
key_t get_shm_key(void) {
    key_t key = ftok(IPC_PATH, SHM_PROJ_ID);
    if (key == -1) {
        perror("ftok shm");
        return -1;
    }
    return key;
}

/* Generate IPC key for semaphores */
key_t get_sem_key(void) {
    key_t key = ftok(IPC_PATH, SEM_PROJ_ID);
    if (key == -1) {
        perror("ftok sem");
        return -1;
    }
    return key;
}

/* Create IPC resources - called by main process */
int ipc_create(IpcResources *ipc) {
    memset(ipc, 0, sizeof(IpcResources));
    ipc->is_owner = 1;

    /* Create shared memory segment */
    key_t shm_key = get_shm_key();
    if (shm_key == -1) return -1;

    ipc->shm_id = shmget(shm_key, sizeof(SharedState), IPC_CREAT | IPC_EXCL | 0600);
    if (ipc->shm_id == -1) {
        if (errno == EEXIST) {
            LOG_WARN("Shared memory already exists, removing old...");
            int old_id = shmget(shm_key, sizeof(SharedState), 0600);
            if (old_id != -1) {
                shmctl(old_id, IPC_RMID, NULL);
            }
            ipc->shm_id = shmget(shm_key, sizeof(SharedState), IPC_CREAT | IPC_EXCL | 0600);
        }
        if (ipc->shm_id == -1) {
            perror("shmget create");
            return -1;
        }
    }
    LOG_DEBUG("Created shared memory: id=%d, size=%zu", ipc->shm_id, sizeof(SharedState));

    /* Attach shared memory */
    ipc->state = (SharedState *)shmat(ipc->shm_id, NULL, 0);
    if (ipc->state == (void *)-1) {
        perror("shmat");
        shmctl(ipc->shm_id, IPC_RMID, NULL);
        return -1;
    }
    LOG_DEBUG("Attached shared memory at %p", (void*)ipc->state);

    /* Initialize shared state */
    ipc_init_state(ipc->state);

    /* Create semaphore set */
    key_t sem_key = get_sem_key();
    if (sem_key == -1) {
        shmdt(ipc->state);
        shmctl(ipc->shm_id, IPC_RMID, NULL);
        return -1;
    }

    ipc->sem_id = semget(sem_key, SEM_COUNT, IPC_CREAT | IPC_EXCL | 0600);
    if (ipc->sem_id == -1) {
        if (errno == EEXIST) {
            LOG_WARN("Semaphore set already exists, removing old...");
            int old_id = semget(sem_key, SEM_COUNT, 0600);
            if (old_id != -1) {
                semctl(old_id, 0, IPC_RMID);
            }
            ipc->sem_id = semget(sem_key, SEM_COUNT, IPC_CREAT | IPC_EXCL | 0600);
        }
        if (ipc->sem_id == -1) {
            perror("semget create");
            shmdt(ipc->state);
            shmctl(ipc->shm_id, IPC_RMID, NULL);
            return -1;
        }
    }
    LOG_DEBUG("Created semaphore set: id=%d, count=%d", ipc->sem_id, SEM_COUNT);

    /* Initialize semaphores */
    if (ipc_init_semaphores(ipc) == -1) {
        shmdt(ipc->state);
        shmctl(ipc->shm_id, IPC_RMID, NULL);
        semctl(ipc->sem_id, 0, IPC_RMID);
        return -1;
    }

    return 0;
}

/* Attach to existing IPC resources - called by child processes */
int ipc_attach(IpcResources *ipc) {
    memset(ipc, 0, sizeof(IpcResources));
    ipc->is_owner = 0;

    /* Get shared memory */
    key_t shm_key = get_shm_key();
    if (shm_key == -1) return -1;

    ipc->shm_id = shmget(shm_key, sizeof(SharedState), 0600);
    if (ipc->shm_id == -1) {
        perror("shmget attach");
        return -1;
    }

    /* Attach shared memory */
    ipc->state = (SharedState *)shmat(ipc->shm_id, NULL, 0);
    if (ipc->state == (void *)-1) {
        perror("shmat");
        return -1;
    }

    /* Get semaphore set */
    key_t sem_key = get_sem_key();
    if (sem_key == -1) {
        shmdt(ipc->state);
        return -1;
    }

    ipc->sem_id = semget(sem_key, SEM_COUNT, 0600);
    if (ipc->sem_id == -1) {
        perror("semget attach");
        shmdt(ipc->state);
        return -1;
    }

    LOG_DEBUG("Attached to IPC: shm_id=%d, sem_id=%d", ipc->shm_id, ipc->sem_id);
    return 0;
}

/* Detach from shared memory */
int ipc_detach(IpcResources *ipc) {
    if (ipc->state != NULL && ipc->state != (void *)-1) {
        if (shmdt(ipc->state) == -1) {
            perror("shmdt");
            return -1;
        }
        ipc->state = NULL;
    }
    return 0;
}

/* Destroy IPC resources - called by main process */
int ipc_destroy(IpcResources *ipc) {
    int errors = 0;

    /* Detach shared memory */
    if (ipc->state != NULL && ipc->state != (void *)-1) {
        if (shmdt(ipc->state) == -1) {
            perror("shmdt");
            errors++;
        }
        ipc->state = NULL;
    }

    /* Remove shared memory segment */
    if (ipc->shm_id > 0) {
        if (shmctl(ipc->shm_id, IPC_RMID, NULL) == -1) {
            perror("shmctl IPC_RMID");
            errors++;
        } else {
            LOG_DEBUG("Removed shared memory id=%d", ipc->shm_id);
        }
        ipc->shm_id = 0;
    }

    /* Remove semaphore set */
    if (ipc->sem_id > 0) {
        if (semctl(ipc->sem_id, 0, IPC_RMID) == -1) {
            perror("semctl IPC_RMID");
            errors++;
        } else {
            LOG_DEBUG("Removed semaphore set id=%d", ipc->sem_id);
        }
        ipc->sem_id = 0;
    }

    return errors ? -1 : 0;
}

/* Initialize semaphores to starting values */
int ipc_init_semaphores(IpcResources *ipc) {
    unsigned short values[SEM_COUNT];

    values[SEM_MUTEX] = 1;                      /* Binary mutex */
    values[SEM_CASHIER_READY] = 0;              /* Cashier not ready yet */
    values[SEM_CASHIER_QUEUE] = 1;              /* One tourist at cashier */
    values[SEM_CASHIER_REQUEST] = 0;            /* No request pending */
    values[SEM_CASHIER_RESPONSE] = 0;           /* No response ready */
    values[SEM_STATION_CAPACITY] = STATION_CAPACITY;  /* Max tourists in station */
    values[SEM_CHAIRS_AVAILABLE] = NUM_CHAIRS * SLOTS_PER_CHAIR;  /* Total chair slots */
    values[SEM_EXIT_CAPACITY] = EXIT_CAPACITY;  /* Exit trail capacity */

    union semun arg;
    arg.array = values;

    if (semctl(ipc->sem_id, 0, SETALL, arg) == -1) {
        perror("semctl SETALL");
        return -1;
    }

    LOG_DEBUG("Initialized %d semaphores", SEM_COUNT);
    return 0;
}

/* Initialize shared state */
void ipc_init_state(SharedState *state) {
    memset(state, 0, sizeof(SharedState));

    state->ropeway_state = STATE_STOPPED;
    state->chairs_available_slots = NUM_CHAIRS * SLOTS_PER_CHAIR;
    state->next_tourist_id = 1;

    LOG_DEBUG("Initialized shared state");
}
