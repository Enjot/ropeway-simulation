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
        write(STDERR_FILENO, "[SIGNAL] [UPPER_WORKER] Emergency stop (SIGUSR1)\n", 49);
    } else if (sig == SIGUSR2) {
        g_resume_signal = 1;
        write(STDERR_FILENO, "[SIGNAL] [UPPER_WORKER] Resume request (SIGUSR2)\n", 49);
    }
}

// Handle emergency stop
static void handle_emergency_stop(IPCResources *res) {
    log_warn("UPPER_WORKER", "Emergency stop acknowledged");

    if (sem_wait(res->sem_id, SEM_STATE) == -1) {
        return;  // Shutdown in progress
    }
    res->state->emergency_stop = 1;
    sem_post(res->sem_id, SEM_STATE);
}

// Issue #1 fix: Handle resume request with separate semaphores to avoid deadlock
static void handle_resume(IPCResources *res) {
    log_info("UPPER_WORKER", "Resume request received - signaling ready");

    // Signal that upper worker is ready
    sem_post(res->sem_id, SEM_UPPER_READY);

    // Wait for lower worker to be ready
    if (sem_wait(res->sem_id, SEM_LOWER_READY) == -1) {
        return;  // Shutdown in progress
    }

    // Clear emergency stop (lower worker also clears it, but be safe)
    if (sem_wait(res->sem_id, SEM_STATE) == -1) {
        return;  // Shutdown in progress
    }
    res->state->emergency_stop = 0;
    sem_post(res->sem_id, SEM_STATE);

    log_info("UPPER_WORKER", "Chairlift resumed");
}

// Issue #11 fix: Use consolidated ipc_check_pause() instead of duplicated code
// See ipc.c for implementation

void upper_worker_main(IPCResources *res, IPCKeys *keys) {
    (void)keys;

    // Initialize logger with component type
    logger_init(res->state, LOG_UPPER_WORKER);

    // Install signal handlers
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    log_info("UPPER_WORKER", "Upper platform worker ready");

    int arrivals_count = 0;

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
    }

    log_info("UPPER_WORKER", "Upper platform worker shutting down (processed %d arrivals)",
             arrivals_count);
}
