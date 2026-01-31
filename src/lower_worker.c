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

static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_running = 0;
    } else if (sig == SIGUSR1) {
        g_emergency_signal = 1;
        write(STDERR_FILENO, "[SIGNAL] [LOWER_WORKER] Emergency stop (SIGUSR1)\n", 49);
    } else if (sig == SIGUSR2) {
        g_resume_signal = 1;
        write(STDERR_FILENO, "[SIGNAL] [LOWER_WORKER] Resume request (SIGUSR2)\n", 49);
    }
}

// Handle emergency stop
static void handle_emergency_stop(IPCResources *res) {
    log_warn("LOWER_WORKER", "Emergency stop triggered - chairlift halted");

    if (sem_wait(res->sem_id, SEM_STATE) == -1) {
        return;  // Shutdown in progress
    }
    res->state->emergency_stop = 1;
    sem_post(res->sem_id, SEM_STATE);

    // Signal upper worker about emergency
    if (res->state->upper_worker_pid > 0) {
        kill(res->state->upper_worker_pid, SIGUSR1);
    }
}

// Issue #1 fix: Handle resume request with separate semaphores to avoid deadlock
static void handle_resume(IPCResources *res) {
    log_info("LOWER_WORKER", "Resume request received - waiting for upper worker");

    // Signal that lower worker is ready
    sem_post(res->sem_id, SEM_LOWER_READY);

    // Wait for upper worker to be ready
    if (sem_wait(res->sem_id, SEM_UPPER_READY) == -1) {
        return;  // Shutdown in progress
    }

    // Clear emergency stop
    if (sem_wait(res->sem_id, SEM_STATE) == -1) {
        return;  // Shutdown in progress
    }
    res->state->emergency_stop = 0;
    sem_post(res->sem_id, SEM_STATE);

    // Issue #4 fix: Release all emergency waiters
    ipc_release_emergency_waiters(res);

    log_info("LOWER_WORKER", "Chairlift resumed");
}

// Issue #11 fix: Use consolidated ipc_check_pause() instead of duplicated code
// See ipc.c for implementation

void lower_worker_main(IPCResources *res, IPCKeys *keys) {
    (void)keys;

    // Install signal handlers
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    log_info("LOWER_WORKER", "Lower platform worker ready");

    int current_chair_slots = 0;  // Slots used on current chair being loaded
    int chair_number = 0;         // For logging

    while (g_running && res->state->running) {
        // Handle signals
        if (g_emergency_signal) {
            g_emergency_signal = 0;
            handle_emergency_stop(res);
        }

        if (g_resume_signal) {
            g_resume_signal = 0;
            handle_resume(res);
        }

        // Issue #11 fix: Use consolidated pause check
        ipc_check_pause(res);

        // Issue #2, #4 fix: Check emergency_stop with proper synchronization
        if (sem_wait(res->sem_id, SEM_STATE) == -1) {
            continue;  // Check loop condition on failure
        }
        int emergency = res->state->emergency_stop;
        sem_post(res->sem_id, SEM_STATE);

        if (emergency) {
            // Issue #4 fix: Block on semaphore instead of polling with usleep
            ipc_wait_emergency_clear(res);
            continue;
        }

        // Issue #16 fix: Clarify comment - msgrcv with -2 returns lowest mtype first
        // (1=VIP before 2=regular, so VIPs are processed first)
        PlatformMsg msg;
        ssize_t ret = msgrcv(res->mq_platform_id, &msg, sizeof(msg) - sizeof(long), -2, 0);

        if (ret == -1) {
            if (errno == EINTR) {
                continue;  // Interrupted by signal
            }
            if (errno == EIDRM) {
                log_debug("LOWER_WORKER", "Platform queue removed, exiting");
                break;
            }
            perror("lower_worker: msgrcv platform");
            continue;
        }

        // Issue #2 fix: Check emergency stop with semaphore protection
        if (sem_wait(res->sem_id, SEM_STATE) == -1) {
            continue;  // Check loop condition on failure
        }
        emergency = res->state->emergency_stop;
        sem_post(res->sem_id, SEM_STATE);

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
            // Doesn't fit - dispatch current chair, start new one
            if (current_chair_slots > 0) {
                log_debug("LOWER_WORKER", "Chair %d departed with %d/%d slots",
                         chair_number, current_chair_slots, CHAIR_CAPACITY);
            }
            current_chair_slots = 0;
            chair_number++;

            // Put tourist back in queue with high priority
            msg.mtype = 1;  // VIP priority so they're next
            if (msgsnd(res->mq_platform_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                // Issue #6 fix: Check for EIDRM
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

        // Tourist fits - confirm boarding
        PlatformMsg response;
        response.mtype = msg.tourist_id;
        response.tourist_id = msg.tourist_id;

        if (msgsnd(res->mq_boarding_id, &response, sizeof(response) - sizeof(long), 0) == -1) {
            if (errno == EINTR) continue;
            // Issue #6 fix: Check for EIDRM
            if (errno == EIDRM) {
                log_debug("LOWER_WORKER", "Boarding queue removed");
                break;
            }
            perror("lower_worker: msgsnd boarding");
            continue;
        }

        current_chair_slots += slots_needed;

        const char *type_name = msg.tourist_type == TOURIST_WALKER ? "walker" : "cyclist";
        if (msg.kid_count > 0) {
            log_info("LOWER_WORKER", "Tourist %d (%s) + %d kid(s) (%d slots) boarded chair %d [%d/%d slots]",
                     msg.tourist_id, type_name, msg.kid_count, slots_needed,
                     chair_number, current_chair_slots, CHAIR_CAPACITY);
        } else {
            log_info("LOWER_WORKER", "Tourist %d (%s, %d slots) boarded chair %d [%d/%d slots]",
                     msg.tourist_id, type_name, slots_needed,
                     chair_number, current_chair_slots, CHAIR_CAPACITY);
        }

        // If chair is full, dispatch and reset
        if (current_chair_slots >= CHAIR_CAPACITY) {
            log_debug("LOWER_WORKER", "Chair %d full, departing", chair_number);
            current_chair_slots = 0;
            chair_number++;
        }
    }

    log_info("LOWER_WORKER", "Lower platform worker shutting down");
}
