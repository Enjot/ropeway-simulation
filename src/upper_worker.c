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
static double g_last_danger_time_sim = 0.0;       // Last danger detection (sim minutes)
static double g_emergency_start_time_sim = 0.0;  // When current emergency started (sim minutes)
static int g_is_emergency_initiator = 0;         // 1 if this worker detected danger

/**
 * Get the appropriate logging tag for a tourist based on type.
 * Returns "FAMILY" for families, "CYCLIST" for cyclists, "TOURIST" for solo walkers.
 */
static const char *get_tourist_tag(TouristType type) {
    if (type == TOURIST_CYCLIST) return "CYCLIST";
    if (type == TOURIST_FAMILY) return "FAMILY";
    return "TOURIST";
}

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
    // Store simulated time for consistent cooldown calculation (already accounts for pause)
    g_emergency_start_time_sim = time_get_sim_minutes_f(res->state);

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
        // Kernel handles SIGTSTP automatically
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
        // Kernel handles SIGTSTP automatically while waiting
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
        // Kernel handles SIGTSTP automatically
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
        // Kernel handles SIGTSTP automatically while waiting
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
        // Kernel handles SIGTSTP automatically
    }
    res->state->emergency_stop = 0;
    sem_post(res->sem_id, SEM_STATE, 1);

    // Release any tourist waiters
    ipc_release_emergency_waiters(res);

    g_is_emergency_initiator = 0;
    g_emergency_start_time_sim = 0.0;

    log_info("UPPER_WORKER", "Chairlift resumed");
}

// Note: ipc_check_pause() removed - kernel handles SIGTSTP/SIGCONT automatically.
// Time Server handles pause offset calculation.

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

    // Check cooldown period using simulated time (already accounts for pause)
    double now_sim = time_get_sim_minutes_f(res->state);
    int cooldown_sim = res->state->danger_cooldown_sim;

    if (g_last_danger_time_sim > 0) {
        if ((now_sim - g_last_danger_time_sim) < cooldown_sim) {
            return 0;  // Still in cooldown
        }
    }

    // Random check
    if ((rand() % 100) < probability) {
        g_last_danger_time_sim = now_sim;  // Store simulated time
        trigger_emergency_stop(res);
        return 1;
    }
    return 0;
}

// ============================================================================
// Chair tracking for atomic SEM_CHAIRS release
// ============================================================================

#define MAX_ACTIVE_CHAIRS 64  // Track up to 64 chairs in transit

typedef struct {
    int chair_id;
    int expected;   // tourists_on_chair
    int arrived;    // count of tourists arrived so far
} ChairTracker;

static ChairTracker g_chairs[MAX_ACTIVE_CHAIRS];
static int g_chair_count = 0;

/**
 * Find or create a tracker for a chair.
 * Returns pointer to tracker, or NULL if tracking array is full.
 */
static ChairTracker* get_chair_tracker(int chair_id, int tourists_on_chair) {
    // Find existing tracker
    for (int i = 0; i < g_chair_count; i++) {
        if (g_chairs[i].chair_id == chair_id) {
            return &g_chairs[i];
        }
    }

    // Create new tracker
    if (g_chair_count < MAX_ACTIVE_CHAIRS) {
        g_chairs[g_chair_count].chair_id = chair_id;
        g_chairs[g_chair_count].expected = tourists_on_chair;
        g_chairs[g_chair_count].arrived = 0;
        return &g_chairs[g_chair_count++];
    }

    return NULL;  // Tracking array full (should not happen in normal operation)
}

/**
 * Remove a completed chair from tracking by index.
 * Uses swap-and-pop for O(1) removal.
 */
static void remove_chair_tracker(int idx) {
    if (idx >= 0 && idx < g_chair_count) {
        g_chairs[idx] = g_chairs[--g_chair_count];
    }
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

    // Signal that this worker is ready (startup barrier)
    ipc_signal_worker_ready(res);
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

        // Note: Pause handled by kernel SIGTSTP automatically

        // Check if we're the emergency initiator and need to wait for cooldown
        if (g_is_emergency_initiator && g_emergency_start_time_sim > 0) {
            // Use simulated time for cooldown (already accounts for pause)
            double now_sim = time_get_sim_minutes_f(res->state);
            int cooldown_sim = res->state->danger_cooldown_sim;

            if ((now_sim - g_emergency_start_time_sim) >= cooldown_sim) {
                // Cooldown passed - initiate resume
                initiate_resume(res);
            } else {
                // Still in cooldown - sleep briefly
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

        const char *tag = get_tourist_tag(msg.tourist_type);
        if (msg.kid_count > 0) {
            log_info(tag, "Tourist %d + %d kid(s) arrived at upper platform (total arrivals: %d)",
                     msg.tourist_id, msg.kid_count, arrivals_count);
        } else {
            log_info(tag, "Tourist %d arrived at upper platform (total arrivals: %d)",
                     msg.tourist_id, arrivals_count);
        }

        // Track chair arrivals for atomic SEM_CHAIRS release
        ChairTracker *tracker = get_chair_tracker(msg.chair_id, msg.tourists_on_chair);
        if (tracker) {
            tracker->arrived++;

            // Check if all tourists from this chair have arrived
            if (tracker->arrived >= tracker->expected) {
                sem_post(res->sem_id, SEM_CHAIRS, 1);

                // Get available chairs count after releasing (for logging)
                int chairs_available = sem_getval(res->sem_id, SEM_CHAIRS);

                log_debug("UPPER_WORKER", "Chair %d complete (%d/%d tourists), releasing slot [chairs available: %d/%d]",
                          msg.chair_id, tracker->arrived, tracker->expected,
                          chairs_available, MAX_CHAIRS_IN_TRANSIT);

                // Remove from tracking
                int idx = (int)(tracker - g_chairs);
                remove_chair_tracker(idx);
            }
        }

        // Check for random danger after each arrival
        check_for_danger(res);
    }

    log_info("UPPER_WORKER", "Upper platform worker shutting down (processed %d arrivals)",
             arrivals_count);
}
