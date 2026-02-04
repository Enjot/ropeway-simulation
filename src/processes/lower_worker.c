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
static int g_alarm_signal = 0;
static double g_last_danger_time_sim = 0.0;       // Last danger detection (sim minutes)
static double g_emergency_start_time_sim = 0.0;  // When current emergency started (sim minutes)
static int g_is_emergency_initiator = 0;         // 1 if this worker detected danger

// State pointers for worker_emergency functions
static WorkerEmergencyState g_emergency_state;

/**
 * @brief Buffered tourist awaiting chair departure.
 */
typedef struct {
    int tourist_id;
    int slots_needed;
} PendingBoarding;

#define MAX_PENDING_PER_CHAIR 4
static PendingBoarding g_pending[MAX_PENDING_PER_CHAIR];
static int g_pending_count = 0;

/**
 * @brief Get the appropriate logging tag for a tourist based on type.
 *
 * @param type Tourist type enum value.
 * @return "FAMILY" for families, "CYCLIST" for cyclists, "TOURIST" for solo walkers.
 */
static const char *get_tourist_tag(TouristType type) {
    if (type == TOURIST_CYCLIST) return "CYCLIST";
    if (type == TOURIST_FAMILY) return "FAMILY";
    return "TOURIST";
}

// Use macro-generated signal handler for emergency-capable workers
DEFINE_EMERGENCY_SIGNAL_HANDLER(signal_handler, "LOWER_WORKER")

/**
 * @brief Check for random danger and trigger emergency stop if detected.
 *
 * Uses pause-adjusted time for cooldown calculation.
 *
 * @param res IPC resources for emergency coordination.
 * @return 1 if danger was detected, 0 otherwise.
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
        worker_trigger_emergency_stop(res, WORKER_LOWER, &g_emergency_state);
        return 1;
    }
    return 0;
}

/**
 * @brief Dispatch the current chair with all buffered tourists.
 *
 * Acquires a chair slot, then sends boarding confirmations to all buffered
 * tourists with the same departure_time so they arrive together.
 * The upper_worker releases the chair slot when all tourists have arrived.
 *
 * @param res IPC resources for semaphores and message queues.
 * @param chair_number Chair identifier for logging.
 * @param slots_used Total slots used on this chair.
 */
static void dispatch_chair(IPCResources *res, int chair_number, int slots_used) {
    if (g_pending_count == 0) {
        return;  // No tourists to dispatch
    }

    // Acquire chair slot (blocks if 36 chairs already in transit)
    if (sem_wait_pauseable(res, SEM_CHAIRS, 1) == -1) {
        return;  // Interrupted/shutdown
    }

    // Get available chairs count after acquiring (for logging)
    int chairs_available = sem_getval(res->sem_id, SEM_CHAIRS);

    time_t departure_time = time(NULL);
    int tourists_on_chair = g_pending_count;

    log_info("LOWER_WORKER", "Chair %d departed with %d tourists (%d/%d slots) [chairs available: %d/%d]",
             chair_number, tourists_on_chair, slots_used, CHAIR_CAPACITY,
             chairs_available, MAX_CHAIRS_IN_TRANSIT);

    // Send boarding confirmations to all buffered tourists
    for (int i = 0; i < g_pending_count; i++) {
        PlatformMsg response;
        memset(&response, 0, sizeof(response));
        response.mtype = g_pending[i].tourist_id;
        response.tourist_id = g_pending[i].tourist_id;
        response.departure_time = departure_time;
        response.chair_id = chair_number;
        response.tourists_on_chair = tourists_on_chair;

        if (msgsnd(res->mq_boarding_id, &response, sizeof(response) - sizeof(long), 0) == -1) {
            // EINVAL can occur during shutdown when queue is being destroyed
            if (errno != EINTR && errno != EIDRM && errno != EINVAL) {
                perror("lower_worker: msgsnd boarding dispatch");
            }
            break;  // Stop trying to send if queue is gone
        }
    }

    g_pending_count = 0;
}

/**
 * @brief Lower platform worker process entry point.
 *
 * Manages tourist boarding onto chairlift. Buffers tourists until chair is full
 * or queue is empty, then dispatches. Handles emergency stops and random danger
 * detection.
 *
 * @param res IPC resources (message queues, semaphores, shared memory).
 * @param keys IPC keys (unused, kept for interface consistency).
 */
void lower_worker_main(IPCResources *res, IPCKeys *keys) {
    (void)keys;

    // Initialize emergency state pointers
    g_emergency_state.is_initiator = &g_is_emergency_initiator;
    g_emergency_state.start_time_sim = &g_emergency_start_time_sim;

    // Initialize logger with component type
    logger_init(res->state, LOG_LOWER_WORKER);
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
    sigaction(SIGALRM, &sa, NULL);

    // Seed random number generator for danger detection
    srand((unsigned int)(time(NULL) ^ getpid()));

    // Signal that this worker is ready (startup barrier)
    if (ipc_signal_worker_ready(res) == -1) {
        log_error("LOWER_WORKER", "Failed to signal ready, exiting");
        return;
    }
    log_info("LOWER_WORKER", "Lower platform worker ready");

    int current_chair_slots = 0;  // Slots used on current chair being loaded
    int chair_number = 1;         // For logging (1-indexed for user-friendliness)

    while (g_running && res->state->running) {
        // Handle SIGUSR1 - emergency stop from upper worker
        if (g_emergency_signal) {
            g_emergency_signal = 0;
            // Receiving worker: acknowledge and wait (blocks until resume)
            worker_acknowledge_emergency_stop(res, WORKER_LOWER, &g_emergency_state);
            continue;  // After resume, continue main loop
        }

        // Handle SIGUSR2 - resume signal (only relevant for receiver, currently no-op)
        if (g_resume_signal) {
            g_resume_signal = 0;
            // Resume signal received - logging only, actual resume handled by semaphores
            log_debug("LOWER_WORKER", "Resume signal (SIGUSR2) received");
        }

        // Check if we're the emergency initiator and need to wait for duration
        if (g_is_emergency_initiator && g_emergency_start_time_sim > 0) {
            // Use simulated time for duration (already accounts for pause)
            double now_sim = time_get_sim_minutes_f(res->state);
            int duration_sim = res->state->danger_duration_sim;

            if ((now_sim - g_emergency_start_time_sim) >= duration_sim) {
                // Duration passed - initiate resume
                worker_initiate_resume(res, WORKER_LOWER, &g_emergency_state);
            } else {
                // Still in cooldown - wait with SIGALRM timeout (100ms)
                ualarm(100000, 0);
                pause();  // Interrupted by SIGALRM or other signals
                ualarm(0, 0);
            }
            continue;
        }

        // Check emergency_stop (for re-entry after signal interrupts)
        if (sem_wait(res->sem_id, SEM_STATE, 1) == -1) {
            continue;  // Check loop condition on failure
        }
        int emergency = res->state->emergency_stop;
        sem_post(res->sem_id, SEM_STATE, 1);

        if (emergency) {
            // Emergency is active but we're not the initiator - acknowledge and wait
            worker_acknowledge_emergency_stop(res, WORKER_LOWER, &g_emergency_state);
            continue;
        }

        // msgrcv with -2 returns lowest mtype first (1=VIP before 2=regular)
        // Use blocking msgrcv with SIGALRM timeout for periodic chair dispatch
        PlatformMsg msg;
        ualarm(100000, 0);  // 100ms timeout for periodic dispatch
        ssize_t ret = msgrcv(res->mq_platform_id, &msg, sizeof(msg) - sizeof(long), -2, 0);
        ualarm(0, 0);  // Cancel alarm if message received

        if (ret == -1) {
            if (errno == EINTR) {
                // Interrupted by signal (SIGALRM or other) - dispatch any pending chair
                if (g_pending_count > 0) {
                    dispatch_chair(res, chair_number, current_chair_slots);
                    current_chair_slots = 0;
                    chair_number = (chair_number % TOTAL_CHAIRS) + 1;
                }
                continue;
            }
            if (errno == EIDRM) {
                log_debug("LOWER_WORKER", "Platform queue removed, exiting");
                break;
            }
            perror("lower_worker: msgrcv platform");
            continue;
        }

        if (sem_wait(res->sem_id, SEM_STATE, 1) == -1) {
            continue;  // Check loop condition on failure
        }
        emergency = res->state->emergency_stop;
        sem_post(res->sem_id, SEM_STATE, 1);

        if (emergency) {
            // Put tourist back in queue with high priority
            msg.mtype = 1;
            if (msgsnd(res->mq_platform_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                if (errno != EINTR && errno != EIDRM) {
                    perror("lower_worker: msgsnd requeue emergency");
                }
            }
            continue;
        }

        int slots_needed = msg.slots_needed;

        // Check if tourist fits on current chair
        if (current_chair_slots + slots_needed > CHAIR_CAPACITY) {
            // Doesn't fit - dispatch current chair with buffered tourists, start new one
            if (g_pending_count > 0) {
                dispatch_chair(res, chair_number, current_chair_slots);
            }
            current_chair_slots = 0;
            chair_number = (chair_number % TOTAL_CHAIRS) + 1;

            // Put tourist back in queue with high priority
            msg.mtype = 1;  // VIP priority so they're next
            if (msgsnd(res->mq_platform_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                if (errno == EIDRM) {
                    log_debug("LOWER_WORKER", "Platform queue removed during requeue");
                    break;
                }
                if (errno != EINTR) {
                    perror("lower_worker: msgsnd requeue");
                }
            }
            continue;
        }

        // Tourist fits - buffer them (don't send confirmation yet)
        if (g_pending_count < MAX_PENDING_PER_CHAIR) {
            g_pending[g_pending_count].tourist_id = msg.tourist_id;
            g_pending[g_pending_count].slots_needed = slots_needed;
            g_pending_count++;
        }

        current_chair_slots += slots_needed;

        const char *tag = get_tourist_tag(msg.tourist_type);
        const char *type_names[] = {"walker", "cyclist", "family"};
        const char *type_name = type_names[msg.tourist_type];
        if (msg.kid_count > 0) {
            log_info(tag, "Tourist %d (%s) + %d kid(s) (%d slots) boarded chair %d [%d/%d slots]",
                     msg.tourist_id, type_name, msg.kid_count, slots_needed,
                     chair_number, current_chair_slots, CHAIR_CAPACITY);
        } else {
            log_info(tag, "Tourist %d (%s, %d slots) boarded chair %d [%d/%d slots]",
                     msg.tourist_id, type_name, slots_needed,
                     chair_number, current_chair_slots, CHAIR_CAPACITY);
        }

        // If chair is full, dispatch and reset
        if (current_chair_slots >= CHAIR_CAPACITY) {
            dispatch_chair(res, chair_number, current_chair_slots);
            current_chair_slots = 0;
            chair_number = (chair_number % TOTAL_CHAIRS) + 1;
        }

        // Check for random danger after each boarding
        check_for_danger(res);
    }

    // Dispatch any remaining pending tourists before shutdown
    if (g_pending_count > 0) {
        dispatch_chair(res, chair_number, current_chair_slots);
    }

    log_info("LOWER_WORKER", "Lower platform worker shutting down");
}
