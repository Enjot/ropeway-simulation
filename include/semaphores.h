#ifndef SEMAPHORES_H
#define SEMAPHORES_H

#include <sys/sem.h>
#include <errno.h>
#include "logger.h"

/* Semaphore indices for MVP */
typedef enum {
    SEM_MUTEX = 0,          /* Protects shared state */
    SEM_CASHIER_READY,      /* Cashier startup signal */
    SEM_CASHIER_QUEUE,      /* Only one tourist at cashier at a time */
    SEM_CASHIER_REQUEST,    /* Tourist has pending request */
    SEM_CASHIER_RESPONSE,   /* Cashier has response ready */
    SEM_STATION_CAPACITY,   /* Max tourists in station */
    SEM_CHAIRS_AVAILABLE,   /* Available chair slots */
    SEM_EXIT_CAPACITY,      /* Trail exit capacity */
    SEM_COUNT               /* Total number of semaphores */
} SemIndex;

/* Union for semctl - required by some systems */
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

/* Semaphore wait (P operation) - with retry on EINTR */
static inline int sem_wait_safe(int semid, int semnum) {
    struct sembuf op = {
        .sem_num = semnum,
        .sem_op = -1,
        .sem_flg = SEM_UNDO
    };

    while (semop(semid, &op, 1) == -1) {
        if (errno == EINTR) {
            continue;  /* Interrupted by signal, retry */
        }
        perror("semop wait");
        return -1;
    }
    return 0;
}

/* Semaphore signal (V operation) */
static inline int sem_signal_safe(int semid, int semnum) {
    struct sembuf op = {
        .sem_num = semnum,
        .sem_op = 1,
        .sem_flg = SEM_UNDO
    };

    while (semop(semid, &op, 1) == -1) {
        if (errno == EINTR) {
            continue;
        }
        perror("semop signal");
        return -1;
    }
    return 0;
}

/* Semaphore wait with decrement by N */
static inline int sem_wait_n(int semid, int semnum, int n) {
    struct sembuf op = {
        .sem_num = semnum,
        .sem_op = -n,
        .sem_flg = SEM_UNDO
    };

    while (semop(semid, &op, 1) == -1) {
        if (errno == EINTR) continue;
        perror("semop wait_n");
        return -1;
    }
    return 0;
}

/* Semaphore signal with increment by N */
static inline int sem_signal_n(int semid, int semnum, int n) {
    struct sembuf op = {
        .sem_num = semnum,
        .sem_op = n,
        .sem_flg = SEM_UNDO
    };

    while (semop(semid, &op, 1) == -1) {
        if (errno == EINTR) continue;
        perror("semop signal_n");
        return -1;
    }
    return 0;
}

/* Try wait (non-blocking) - returns 0 on success, -1 if would block */
static inline int sem_trywait(int semid, int semnum) {
    struct sembuf op = {
        .sem_num = semnum,
        .sem_op = -1,
        .sem_flg = SEM_UNDO | IPC_NOWAIT
    };

    if (semop(semid, &op, 1) == -1) {
        if (errno == EAGAIN) {
            return -1;  /* Would block */
        }
        if (errno == EINTR) {
            return -1;  /* Interrupted */
        }
        perror("semop trywait");
        return -1;
    }
    return 0;
}

/* Get current semaphore value */
static inline int sem_getval(int semid, int semnum) {
    return semctl(semid, semnum, GETVAL);
}

/* Set semaphore value */
static inline int sem_setval(int semid, int semnum, int val) {
    union semun arg;
    arg.val = val;
    if (semctl(semid, semnum, SETVAL, arg) == -1) {
        perror("semctl SETVAL");
        return -1;
    }
    return 0;
}

/* Wait for semaphore to become zero (useful for synchronization) */
static inline int sem_wait_zero(int semid, int semnum) {
    struct sembuf op = {
        .sem_num = semnum,
        .sem_op = 0,
        .sem_flg = 0
    };

    while (semop(semid, &op, 1) == -1) {
        if (errno == EINTR) continue;
        perror("semop wait_zero");
        return -1;
    }
    return 0;
}

#endif /* SEMAPHORES_H */
