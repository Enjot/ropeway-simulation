/**
 * @file ipc/mq.c
 * @brief System V message queue operations.
 */

#include "ipc/ipc.h"
#include "core/logger.h"

#include <stdio.h>
#include <sys/msg.h>

/**
 * @brief Create all message queues.
 *
 * Creates cashier, platform, boarding, arrivals, and worker message queues.
 *
 * @param res IPC resources structure to populate with queue IDs.
 * @param keys IPC keys for queue creation.
 * @return 0 on success, -1 on error.
 */
int ipc_mq_create(IPCResources *res, const IPCKeys *keys) {
    res->mq_cashier_id = msgget(keys->mq_cashier_key, IPC_CREAT | IPC_EXCL | 0600);
    if (res->mq_cashier_id == -1) {
        perror("ipc_mq_create: msgget cashier");
        return -1;
    }
    log_debug("IPC", "Created cashier message queue: id=%d", res->mq_cashier_id);

    res->mq_platform_id = msgget(keys->mq_platform_key, IPC_CREAT | IPC_EXCL | 0600);
    if (res->mq_platform_id == -1) {
        perror("ipc_mq_create: msgget platform");
        return -1;
    }
    log_debug("IPC", "Created platform message queue: id=%d", res->mq_platform_id);

    res->mq_boarding_id = msgget(keys->mq_boarding_key, IPC_CREAT | IPC_EXCL | 0600);
    if (res->mq_boarding_id == -1) {
        perror("ipc_mq_create: msgget boarding");
        return -1;
    }
    log_debug("IPC", "Created boarding message queue: id=%d", res->mq_boarding_id);

    res->mq_arrivals_id = msgget(keys->mq_arrivals_key, IPC_CREAT | IPC_EXCL | 0600);
    if (res->mq_arrivals_id == -1) {
        perror("ipc_mq_create: msgget arrivals");
        return -1;
    }
    log_debug("IPC", "Created arrivals message queue: id=%d", res->mq_arrivals_id);

    res->mq_worker_id = msgget(keys->mq_worker_key, IPC_CREAT | IPC_EXCL | 0600);
    if (res->mq_worker_id == -1) {
        perror("ipc_mq_create: msgget worker");
        return -1;
    }
    log_debug("IPC", "Created worker message queue: id=%d", res->mq_worker_id);

    return 0;
}

/**
 * @brief Attach to existing message queues.
 *
 * For child processes to access message queues created by main.
 *
 * @param res IPC resources structure to populate with queue IDs.
 * @param keys IPC keys for queue lookup.
 * @return 0 on success, -1 on error.
 */
int ipc_mq_attach(IPCResources *res, const IPCKeys *keys) {
    res->mq_cashier_id = msgget(keys->mq_cashier_key, 0600);
    if (res->mq_cashier_id == -1) {
        perror("ipc_mq_attach: msgget cashier");
        return -1;
    }

    res->mq_platform_id = msgget(keys->mq_platform_key, 0600);
    if (res->mq_platform_id == -1) {
        perror("ipc_mq_attach: msgget platform");
        return -1;
    }

    res->mq_boarding_id = msgget(keys->mq_boarding_key, 0600);
    if (res->mq_boarding_id == -1) {
        perror("ipc_mq_attach: msgget boarding");
        return -1;
    }

    res->mq_arrivals_id = msgget(keys->mq_arrivals_key, 0600);
    if (res->mq_arrivals_id == -1) {
        perror("ipc_mq_attach: msgget arrivals");
        return -1;
    }

    res->mq_worker_id = msgget(keys->mq_worker_key, 0600);
    if (res->mq_worker_id == -1) {
        perror("ipc_mq_attach: msgget worker");
        return -1;
    }

    return 0;
}

/**
 * @brief Destroy all message queues.
 *
 * Removes all message queues with IPC_RMID.
 *
 * @param res IPC resources containing queue IDs to destroy.
 */
void ipc_mq_destroy(IPCResources *res) {
    if (res->mq_cashier_id != -1) {
        if (msgctl(res->mq_cashier_id, IPC_RMID, NULL) == -1) {
            perror("ipc_mq_destroy: msgctl cashier IPC_RMID");
        }
        res->mq_cashier_id = -1;
    }

    if (res->mq_platform_id != -1) {
        if (msgctl(res->mq_platform_id, IPC_RMID, NULL) == -1) {
            perror("ipc_mq_destroy: msgctl platform IPC_RMID");
        }
        res->mq_platform_id = -1;
    }

    if (res->mq_boarding_id != -1) {
        if (msgctl(res->mq_boarding_id, IPC_RMID, NULL) == -1) {
            perror("ipc_mq_destroy: msgctl boarding IPC_RMID");
        }
        res->mq_boarding_id = -1;
    }

    if (res->mq_arrivals_id != -1) {
        if (msgctl(res->mq_arrivals_id, IPC_RMID, NULL) == -1) {
            perror("ipc_mq_destroy: msgctl arrivals IPC_RMID");
        }
        res->mq_arrivals_id = -1;
    }

    if (res->mq_worker_id != -1) {
        if (msgctl(res->mq_worker_id, IPC_RMID, NULL) == -1) {
            perror("ipc_mq_destroy: msgctl worker IPC_RMID");
        }
        res->mq_worker_id = -1;
    }
}

/**
 * @brief Signal-safe message queue destruction.
 *
 * Uses only msgctl which is async-signal-safe.
 *
 * @param res IPC resources containing queue IDs to destroy.
 */
void ipc_mq_destroy_signal_safe(IPCResources *res) {
    if (res->mq_cashier_id != -1) {
        msgctl(res->mq_cashier_id, IPC_RMID, NULL);
        res->mq_cashier_id = -1;
    }
    if (res->mq_platform_id != -1) {
        msgctl(res->mq_platform_id, IPC_RMID, NULL);
        res->mq_platform_id = -1;
    }
    if (res->mq_boarding_id != -1) {
        msgctl(res->mq_boarding_id, IPC_RMID, NULL);
        res->mq_boarding_id = -1;
    }
    if (res->mq_arrivals_id != -1) {
        msgctl(res->mq_arrivals_id, IPC_RMID, NULL);
        res->mq_arrivals_id = -1;
    }
    if (res->mq_worker_id != -1) {
        msgctl(res->mq_worker_id, IPC_RMID, NULL);
        res->mq_worker_id = -1;
    }
}
