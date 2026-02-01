#include "types.h"
#include "ipc.h"
#include "logger.h"
#include "time_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/msg.h>

static int g_running = 1;
static int g_emergency_signal = 0;
static int g_resume_signal = 0;
static time_t g_last_danger_time = 0;       // Last danger detection (real time)
static time_t g_emergency_start_time = 0;   // When current emergency started
static int g_is_emergency_initiator = 0;    // 1 if this worker detected danger

static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_running = 0;
    } else if (sig == SIGUSR1) {
        g_emergency_signal = 1;
        write(STDERR_FILENO, "[SIGNAL] [UPPER_WORKER] Emergency stop (SIGUSR1)\n", 49);
    } else if (sig == SIGUSR2) {
        g_resume_signal = 1;
        write(STDERR_FILENO, "[SIGNAL] [UPPER_WORKER] Resume request (SIGUSR2)\n", 49);
    }
}

/**
 * Called when THIS worker detects danger.
 * Sets emergency flag, sends SIGUSR1 to other worker, records start time.
 */
static void trigger_emergency_stop(IPCResources *res) {
    log_warn("UPPER_WORKER", "Danger detected! Triggering emergency stop");

    g_is_emergency_initiator = 1;
    // Store pause-adjusted start time for consistent cooldown calculation
    g_emergency_start_time = time(NULL) - res->state->total_pause_offset;

    // Set emergency flag
    if (sem_wait(res->sem_id, SEM_STATE, 1) == -1) {
        return;  // Shutdown in progress
    }
    res->state->emergency_stop = 1;
    sem_post(res->sem_id, SEM_STATE, 1);

    // Signal lower worker about emergency
    if (res->state->lower_worker_pid > 0) {
        kill(res->state->lower_worker_pid, SIGUSR1);
    }
}

/**
 * Called when receiving SIGUSR1 from lower worker.
 * Acknowledges emergency, blocks on message queue until resume is initiated.
 * Handles SIGTSTP (EINTR) by checking pause and retrying.
 */
static void acknowledge_emergency_stop(IPCResources *res) {
    log_warn("UPPER_WORKER", "Emergency stop acknowledged from lower worker");

    g_is_emergency_initiator = 0;

    // Set emergency flag
    while (sem_wait(res->sem_id, SEM_STATE, 1) == -1) {
        if (errno != EINTR) return;  // Shutdown in progress
        ipc_check_pause(res);  // Handle SIGTSTP
    }
    res->state->emergency_stop = 1;
    sem_post(res->sem_id, SEM_STATE, 1);

    // Block until detecting worker says we can resume (via message queue)
    log_debug("UPPER_WORKER", "Waiting for resume message from lower worker...");
    WorkerMsg msg;
    while (msgrcv(res->mq_worker_id, &msg, sizeof(msg) - sizeof(long), WORKER_DEST_UPPER, 0) == -1) {
        if (errno == EIDRM) return;  // Queue removed, shutdown
        if (errno != EINTR) {
            perror("upper_worker: msgrcv worker");
            return;
        }
        ipc_check_pause(res);  // Handle SIGTSTP while waiting
    }

    // Verify message type (should be READY_TO_RESUME)
    if (msg.msg_type != WORKER_MSG_READY_TO_RESUME) {
        log_warn("UPPER_WORKER", "Unexpected message type %d, expected READY_TO_RESUME", msg.msg_type);
    }

    // Signal that we're ready to resume (via message queue)
    log_debug("UPPER_WORKER", "Signaling ready to resume");
    WorkerMsg response = { .mtype = WORKER_DEST_LOWER, .msg_type = WORKER_MSG_I_AM_READY };
    if (msgsnd(res->mq_worker_id, &response, sizeof(response) - sizeof(long), 0) == -1) {
        if (errno != EINTR && errno != EIDRM) {
            perror("upper_worker: msgsnd I_AM_READY");
        }
        return;
    }

    // Clear emergency stop
    while (sem_wait(res->sem_id, SEM_STATE, 1) == -1) {
        if (errno != EINTR) return;  // Shutdown in progress
        ipc_check_pause(res);  // Handle SIGTSTP
    }
    res->state->emergency_stop = 0;
    sem_post(res->sem_id, SEM_STATE, 1);

    log_info("UPPER_WORKER", "Chairlift resumed");
}

/**
 * Called by detecting worker after cooldown to initiate resume.
 * Wakes up receiving worker via message queue, waits for ready confirmation, sends SIGUSR2.
 * Handles SIGTSTP (EINTR) by checking pause and retrying.
 */
static void initiate_resume(IPCResources *res) {
    log_info("UPPER_WORKER", "Cooldown passed, initiating resume");

    // Send READY_TO_RESUME to receiving worker (via message queue)
    WorkerMsg msg = { .mtype = WORKER_DEST_LOWER, .msg_type = WORKER_MSG_READY_TO_RESUME };
    if (msgsnd(res->mq_worker_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
        if (errno != EINTR && errno != EIDRM) {
            perror("upper_worker: msgsnd READY_TO_RESUME");
        }
        return;
    }

    // Wait for I_AM_READY response from other worker (via message queue)
    log_debug("UPPER_WORKER", "Waiting for lower worker to be ready...");
    WorkerMsg response;
    while (msgrcv(res->mq_worker_id, &response, sizeof(response) - sizeof(long), WORKER_DEST_UPPER, 0) == -1) {
        if (errno == EIDRM) return;  // Queue removed, shutdown
        if (errno != EINTR) {
            perror("upper_worker: msgrcv I_AM_READY");
            return;
        }
        ipc_check_pause(res);  // Handle SIGTSTP while waiting
    }

    // Verify message type (should be I_AM_READY)
    if (response.msg_type != WORKER_MSG_I_AM_READY) {
        log_warn("UPPER_WORKER", "Unexpected message type %d, expected I_AM_READY", response.msg_type);
    }

    // Send SIGUSR2 to formally resume chairlift
    if (res->state->lower_worker_pid > 0) {
        kill(res->state->lower_worker_pid, SIGUSR2);
    }

    // Clear emergency stop
    while (sem_wait(res->sem_id, SEM_STATE, 1) == -1) {
        if (errno != EINTR) return;  // Shutdown in progress
        ipc_check_pause(res);  // Handle SIGTSTP
    }
    res->state->emergency_stop = 0;
    sem_post(res->sem_id, SEM_STATE, 1);

    // Release any tourist waiters
    ipc_release_emergency_waiters(res);

    g_is_emergency_initiator = 0;
    g_emergency_start_time = 0;

    log_info("UPPER_WORKER", "Chairlift resumed");
}

// Issue #11 fix: Use consolidated ipc_check_pause() instead of duplicated code
// See ipc.c for implementation

/**
 * Check for random danger and trigger emergency stop if detected.
 * Returns 1 if danger was detected, 0 otherwise.
 * Uses pause-adjusted time for cooldown calculation.
 */
static int check_for_danger(IPCResources *res) {
    int probability = res->state->danger_probability;
    if (probability <= 0) {
        return 0;  // Danger detection disabled
    }

    // Check cooldown period (convert sim minutes to real seconds)
    // Use pause-adjusted time for consistent cooldown tracking
    time_t now = time(NULL);
    time_t adjusted_now = now - res->state->total_pause_offset;

    if (g_last_danger_time > 0) {
        double time_accel = res->state->time_acceleration;
        int cooldown_sim = res->state->danger_cooldown_sim;
        double cooldown_real = (time_accel > 0) ? (cooldown_sim / time_accel) : 60.0;

        if (adjusted_now - g_last_danger_time < (time_t)cooldown_real) {
            return 0;  // Still in cooldown
        }
    }

    // Random check
    if ((rand() % 100) < probability) {
        g_last_danger_time = adjusted_now;  // Store pause-adjusted time
        trigger_emergency_stop(res);
        return 1;
    }
    return 0;
}

void upper_worker_main(IPCResources *res, IPCKeys *keys) {
    (void)keys;

    // Initialize logger with component type
    logger_init(res->state, LOG_UPPER_WORKER);
    logger_set_debug_enabled(res->state->debug_logs_enabled);

    // Install signal handlers
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    // Seed random number generator for danger detection (different seed than lower worker)
    srand((unsigned int)(time(NULL) ^ (getpid() << 1)));

    log_info("UPPER_WORKER", "Upper platform worker ready");

    int arrivals_count = 0;

    while (g_running && res->state->running) {
        // Handle SIGUSR1 - emergency stop from lower worker
        if (g_emergency_signal) {
            g_emergency_signal = 0;
            // Receiving worker: acknowledge and wait (blocks until resume)
            acknowledge_emergency_stop(res);
            continue;  // After resume, continue main loop
        }

        // Handle SIGUSR2 - resume signal (only relevant for receiver, currently no-op)
        if (g_resume_signal) {
            g_resume_signal = 0;
            // Resume signal received - logging only, actual resume handled by semaphores
            log_debug("UPPER_WORKER", "Resume signal (SIGUSR2) received");
        }

        // Issue #11 fix: Use consolidated pause check
        ipc_check_pause(res);

        // Check if we're the emergency initiator and need to wait for cooldown
        if (g_is_emergency_initiator && g_emergency_start_time > 0) {
            // Account for pause time in cooldown calculation
            time_t now = time(NULL);
            time_t pause_offset = res->state->total_pause_offset;
            time_t adjusted_now = now - pause_offset;

            double time_accel = res->state->time_acceleration;
            int cooldown_sim = res->state->danger_cooldown_sim;
            double cooldown_real = (time_accel > 0) ? (cooldown_sim / time_accel) : 60.0;

            if ((adjusted_now - g_emergency_start_time) >= (time_t)cooldown_real) {
                // Cooldown passed - initiate resume
                initiate_resume(res);
            } else {
                // Still in cooldown - check for pause and sleep briefly
                ipc_check_pause(res);
                usleep(100000);  // 100ms (timing only, not IPC sync)
            }
            continue;
        }

        // Wait for tourist arrival notification
        ArrivalMsg msg;
        ssize_t ret = msgrcv(res->mq_arrivals_id, &msg, sizeof(msg) - sizeof(long), 0, 0);

        if (ret == -1) {
            if (errno == EINTR) {
                continue;  // Interrupted by signal
            }
            if (errno == EIDRM) {
                log_debug("UPPER_WORKER", "Arrivals queue removed, exiting");
                break;
            }
            perror("upper_worker: msgrcv arrivals");
            continue;
        }

        // Count parent + kids as separate arrivals
        arrivals_count += (1 + msg.kid_count);

        if (msg.kid_count > 0) {
            log_info("UPPER_WORKER", "Tourist %d + %d kid(s) arrived at upper platform (total arrivals: %d)",
                     msg.tourist_id, msg.kid_count, arrivals_count);
        } else {
            log_info("UPPER_WORKER", "Tourist %d arrived at upper platform (total arrivals: %d)",
                     msg.tourist_id, arrivals_count);
        }

        // Check for random danger after each arrival
        check_for_danger(res);
    }

    log_info("UPPER_WORKER", "Upper platform worker shutting down (processed %d arrivals)",
             arrivals_count);
}
