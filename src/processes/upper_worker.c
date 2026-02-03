#define _GNU_SOURCE

#include "constants.h"
#include "ipc/messages.h"
#include "ipc/ipc.h"
#include "core/logger.h"
#include "core/time_sim.h"
#include "common/signal_common.h"
#include "common/worker_emergency.h"

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

// State pointers for worker_emergency functions
static WorkerEmergencyState g_emergency_state;

/**
 * Get the appropriate logging tag for a tourist based on type.
 * Returns "FAMILY" for families, "CYCLIST" for cyclists, "TOURIST" for solo walkers.
 */
static const char *get_tourist_tag(TouristType type) {
    if (type == TOURIST_CYCLIST) return "CYCLIST";
    if (type == TOURIST_FAMILY) return "FAMILY";
    return "TOURIST";
}

// Use macro-generated signal handler for emergency-capable workers
DEFINE_EMERGENCY_SIGNAL_HANDLER(signal_handler, "UPPER_WORKER")

// Note: Emergency functions moved to worker_emergency.c (shared module)

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

    // Check if still within duration of previous danger (can't trigger new one)
    double now_sim = time_get_sim_minutes_f(res->state);
    int duration_sim = res->state->danger_duration_sim;

    if (g_last_danger_time_sim > 0) {
        if ((now_sim - g_last_danger_time_sim) < duration_sim) {
            return 0;  // Still within previous emergency duration
        }
    }

    // Random check
    if ((rand() % 100) < probability) {
        g_last_danger_time_sim = now_sim;  // Store simulated time
        worker_trigger_emergency_stop(res, WORKER_UPPER, &g_emergency_state);
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

    // Initialize emergency state pointers
    g_emergency_state.is_initiator = &g_is_emergency_initiator;
    g_emergency_state.start_time_sim = &g_emergency_start_time_sim;

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
            worker_acknowledge_emergency_stop(res, WORKER_UPPER, &g_emergency_state);
            continue;  // After resume, continue main loop
        }

        // Handle SIGUSR2 - resume signal (only relevant for receiver, currently no-op)
        if (g_resume_signal) {
            g_resume_signal = 0;
            // Resume signal received - logging only, actual resume handled by semaphores
            log_debug("UPPER_WORKER", "Resume signal (SIGUSR2) received");
        }

        // Note: Pause handled by kernel SIGTSTP automatically

        // Check if we're the emergency initiator and need to wait for duration
        if (g_is_emergency_initiator && g_emergency_start_time_sim > 0) {
            // Use simulated time for duration (already accounts for pause)
            double now_sim = time_get_sim_minutes_f(res->state);
            int duration_sim = res->state->danger_duration_sim;

            if ((now_sim - g_emergency_start_time_sim) >= duration_sim) {
                // Duration passed - initiate resume
                worker_initiate_resume(res, WORKER_UPPER, &g_emergency_state);
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
