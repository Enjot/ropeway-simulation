#include "common/worker_emergency.h"
#include "ipc/messages.h"
#include "core/logger.h"
#include "core/time_sim.h"

#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/msg.h>

/**
 * @brief Get the message queue destination filter for this worker's incoming messages.
 *
 * @param role Worker role (WORKER_LOWER or WORKER_UPPER).
 * @return Message queue destination filter.
 */
static int get_my_dest(WorkerRole role) {
    return (role == WORKER_LOWER) ? WORKER_DEST_LOWER : WORKER_DEST_UPPER;
}

/**
 * @brief Get the message queue destination for sending to the other worker.
 *
 * @param role Worker role (WORKER_LOWER or WORKER_UPPER).
 * @return Message queue destination for the other worker.
 */
static int get_other_dest(WorkerRole role) {
    return (role == WORKER_LOWER) ? WORKER_DEST_UPPER : WORKER_DEST_LOWER;
}

/**
 * @brief Get the PID of the other worker.
 *
 * @param res IPC resources containing worker PIDs.
 * @param role Worker role (WORKER_LOWER or WORKER_UPPER).
 * @return PID of the other worker.
 */
static pid_t get_other_pid(IPCResources *res, WorkerRole role) {
    return (role == WORKER_LOWER) ? res->state->upper_worker_pid : res->state->lower_worker_pid;
}

/**
 * @brief Get the logging tag for this worker.
 *
 * @param role Worker role (WORKER_LOWER or WORKER_UPPER).
 * @return Logging tag string.
 */
static const char* get_worker_tag(WorkerRole role) {
    return (role == WORKER_LOWER) ? "LOWER_WORKER" : "UPPER_WORKER";
}

/**
 * @brief Get the name of the other worker (for logging).
 *
 * @param role Worker role (WORKER_LOWER or WORKER_UPPER).
 * @return Name string of the other worker.
 */
static const char* get_other_worker_name(WorkerRole role) {
    return (role == WORKER_LOWER) ? "upper worker" : "lower worker";
}

/**
 * @brief Initiate emergency stop when danger is detected.
 *
 * Attempts to acquire SEM_EMERGENCY_LOCK to become the initiator.
 * If lock acquired, sets emergency_stop flag and signals other worker via SIGUSR1.
 * If lock not acquired, falls back to receiver role.
 *
 * @param res IPC resources.
 * @param role Worker role (WORKER_LOWER or WORKER_UPPER).
 * @param state Emergency state tracking.
 */
void worker_trigger_emergency_stop(IPCResources *res, WorkerRole role, WorkerEmergencyState *state) {
    const char *tag = get_worker_tag(role);
    log_warn(tag, "Danger detected! Attempting to become emergency initiator");

    // Try to acquire emergency lock - only one worker can be initiator
    if (sem_trywait(res->sem_id, SEM_EMERGENCY_LOCK) == -1) {
        // Failed to acquire lock - another worker is already initiator
        // Become receiver instead
        log_debug(tag, "Another worker is initiator, becoming receiver");
        worker_acknowledge_emergency_stop(res, role, state);
        return;
    }

    // Got the lock - we are the initiator
    log_info(tag, "Acquired emergency lock, becoming initiator");
    *state->is_initiator = 1;
    // Store simulated time for consistent cooldown calculation (already accounts for pause)
    *state->start_time_sim = time_get_sim_minutes_f(res->state);

    // Set emergency flag
    if (sem_wait(res->sem_id, SEM_STATE, 1) == -1) {
        // Shutdown - release lock
        sem_post(res->sem_id, SEM_EMERGENCY_LOCK, 1);
        return;
    }
    res->state->emergency_stop = 1;
    sem_post(res->sem_id, SEM_STATE, 1);

    // Signal other worker about emergency
    pid_t other_pid = get_other_pid(res, role);
    if (other_pid > 0) {
        kill(other_pid, SIGUSR1);
    }
}

/**
 * @brief Acknowledge emergency stop from the other worker.
 *
 * Sets emergency_stop flag, then blocks waiting for resume message via MQ_WORKER.
 * After resume received, signals ready and clears emergency flag.
 *
 * @param res IPC resources.
 * @param role Worker role (WORKER_LOWER or WORKER_UPPER).
 * @param state Emergency state tracking.
 */
void worker_acknowledge_emergency_stop(IPCResources *res, WorkerRole role, WorkerEmergencyState *state) {
    const char *tag = get_worker_tag(role);
    const char *other_name = get_other_worker_name(role);
    int my_dest = get_my_dest(role);
    int other_dest = get_other_dest(role);

    log_warn(tag, "Emergency stop acknowledged from %s", other_name);

    *state->is_initiator = 0;

    // Set emergency flag
    while (sem_wait(res->sem_id, SEM_STATE, 1) == -1) {
        if (errno != EINTR) return;  // Shutdown in progress
        // Kernel handles SIGTSTP automatically
    }
    res->state->emergency_stop = 1;
    sem_post(res->sem_id, SEM_STATE, 1);

    // Block until detecting worker says we can resume (via message queue)
    log_debug(tag, "Waiting for resume message from %s...", other_name);
    WorkerMsg msg;
    while (msgrcv(res->mq_worker_id, &msg, sizeof(msg) - sizeof(long), my_dest, 0) == -1) {
        if (errno == EIDRM) return;  // Queue removed, shutdown
        if (errno != EINTR) {
            perror("worker_acknowledge_emergency_stop: msgrcv worker");
            return;
        }
        // Kernel handles SIGTSTP automatically while waiting
    }

    // Verify message type (should be READY_TO_RESUME)
    if (msg.msg_type != WORKER_MSG_READY_TO_RESUME) {
        log_warn(tag, "Unexpected message type %d, expected READY_TO_RESUME", msg.msg_type);
    }

    // Signal that we're ready to resume (via message queue)
    log_debug(tag, "Signaling ready to resume");
    WorkerMsg response = { .mtype = other_dest, .msg_type = WORKER_MSG_I_AM_READY };
    if (msgsnd(res->mq_worker_id, &response, sizeof(response) - sizeof(long), 0) == -1) {
        if (errno != EINTR && errno != EIDRM) {
            perror("worker_acknowledge_emergency_stop: msgsnd I_AM_READY");
        }
        return;
    }

    // Clear emergency stop
    while (sem_wait(res->sem_id, SEM_STATE, 1) == -1) {
        if (errno != EINTR) return;  // Shutdown in progress
        // Kernel handles SIGTSTP automatically
    }
    res->state->emergency_stop = 0;
    sem_post(res->sem_id, SEM_STATE, 1);

    log_info(tag, "Chairlift resumed");
}

/**
 * @brief Resume operations after emergency cooldown passed.
 *
 * Sends READY_TO_RESUME message to other worker, waits for I_AM_READY response,
 * sends SIGUSR2, clears emergency flag, and releases SEM_EMERGENCY_LOCK.
 *
 * @param res IPC resources.
 * @param role Worker role (WORKER_LOWER or WORKER_UPPER).
 * @param state Emergency state tracking.
 */
void worker_initiate_resume(IPCResources *res, WorkerRole role, WorkerEmergencyState *state) {
    const char *tag = get_worker_tag(role);
    const char *other_name = get_other_worker_name(role);
    int my_dest = get_my_dest(role);
    int other_dest = get_other_dest(role);
    pid_t other_pid = get_other_pid(res, role);

    log_info(tag, "Danger resolved, initiating resume");

    // Send READY_TO_RESUME to receiving worker (via message queue)
    WorkerMsg msg = { .mtype = other_dest, .msg_type = WORKER_MSG_READY_TO_RESUME };
    if (msgsnd(res->mq_worker_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
        if (errno != EINTR && errno != EIDRM) {
            perror("worker_initiate_resume: msgsnd READY_TO_RESUME");
        }
        return;
    }

    // Wait for I_AM_READY response from other worker (via message queue)
    log_debug(tag, "Waiting for %s to be ready...", other_name);
    WorkerMsg response;
    while (msgrcv(res->mq_worker_id, &response, sizeof(response) - sizeof(long), my_dest, 0) == -1) {
        if (errno == EIDRM) return;  // Queue removed, shutdown
        if (errno != EINTR) {
            perror("worker_initiate_resume: msgrcv I_AM_READY");
            return;
        }
        // Kernel handles SIGTSTP automatically while waiting
    }

    // Verify message type (should be I_AM_READY)
    if (response.msg_type != WORKER_MSG_I_AM_READY) {
        log_warn(tag, "Unexpected message type %d, expected I_AM_READY", response.msg_type);
    }

    // Send SIGUSR2 to formally resume chairlift
    if (other_pid > 0) {
        kill(other_pid, SIGUSR2);
    }

    // Clear emergency stop
    while (sem_wait(res->sem_id, SEM_STATE, 1) == -1) {
        if (errno != EINTR) return;  // Shutdown in progress
        // Kernel handles SIGTSTP automatically
    }
    res->state->emergency_stop = 0;
    sem_post(res->sem_id, SEM_STATE, 1);

    // Release any tourist waiters
    ipc_release_emergency_waiters(res);

    // Release emergency lock so another emergency can occur
    sem_post(res->sem_id, SEM_EMERGENCY_LOCK, 1);
    log_debug(tag, "Released emergency lock");

    *state->is_initiator = 0;
    *state->start_time_sim = 0.0;

    log_info(tag, "Chairlift resumed");
}
