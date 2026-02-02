/**
 * @file ipc/keys.c
 * @brief IPC key generation using ftok.
 */

#include "ipc/resources.h"

#include <stdio.h>
#include <sys/ipc.h>

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

    // Note: Keys are logged in ipc_create() to avoid duplicate logs from child processes

    return 0;
}
