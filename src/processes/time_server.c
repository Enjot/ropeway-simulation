/**
 * @file time_server.c
 * @brief Time Server process - centralizes all time management and pause handling
 *
 * The Time Server is responsible for:
 * - Maintaining the current simulated time with sub-millisecond precision
 * - Handling SIGTSTP/SIGCONT pause tracking and offset calculation
 * - Atomically updating SharedState.current_sim_time_ms for other processes to read
 *
 * Other processes simply read the atomic time value - no time calculation needed.
 */

#include "ipc/ipc.h"
#include "core/logger.h"

#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>

// Signal flags
static volatile sig_atomic_t g_running = 1;
static volatile sig_atomic_t g_paused = 0;
static volatile sig_atomic_t g_sigcont_received = 0;

// Pause tracking (local to Time Server)
static struct timespec g_pause_start;
static double g_total_pause_offset = 0.0;  // In seconds

// Real start time with high precision
static struct timespec g_real_start_time;

// Saved sigaction for SIGTSTP reinstallation (used in SIGCONT handler)
static struct sigaction g_sigtstp_action;

/**
 * @brief SIGTSTP handler - capture pause start time and suspend
 *
 * Note: Uses SA_RESETHAND so handler is automatically reset to SIG_DFL
 * before this handler runs. After capture, we raise SIGTSTP again to
 * actually suspend the process. SIGCONT handler will reinstall this handler.
 */
static void sigtstp_handler(int sig) {
    (void)sig;
    if (clock_gettime(CLOCK_MONOTONIC, &g_pause_start) == -1) {
        // Fallback: mark as paused anyway, offset calculation may be inaccurate
        write(STDERR_FILENO, "[SIGNAL] [TIME_SERVER] clock_gettime failed\n", 44);
    }
    g_paused = 1;
    write(STDERR_FILENO, "[SIGNAL] [TIME_SERVER] SIGTSTP received\n", 40);

    // Re-raise SIGTSTP to actually stop (handler was reset to SIG_DFL by SA_RESETHAND)
    raise(SIGTSTP);
}

/**
 * @brief SIGCONT handler - calculate pause offset and resume
 *
 * Note: sigaction() is async-signal-safe (POSIX.1-2008), so we can
 * safely reinstall the SIGTSTP handler here.
 */
static void sigcont_handler(int sig) {
    (void)sig;
    g_sigcont_received = 1;
    write(STDERR_FILENO, "[SIGNAL] [TIME_SERVER] SIGCONT received\n", 40);

    // Reinstall our SIGTSTP handler using sigaction (async-signal-safe)
    sigaction(SIGTSTP, &g_sigtstp_action, NULL);
}

/**
 * @brief SIGTERM/SIGINT handler - request shutdown
 */
static void sigterm_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/**
 * @brief SIGALRM handler - wake up from pause() to update time
 */
static void sigalrm_handler(int sig) {
    (void)sig;
    // Just wake up - time update happens in main loop
}

/**
 * @brief Handle pause offset calculation (called outside signal handler)
 */
static void handle_resume(void) {
    if (!g_sigcont_received) return;
    g_sigcont_received = 0;

    if (g_paused) {
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
            perror("time_server: clock_gettime in handle_resume");
            // Fallback: just mark as unpaused, offset calculation may be inaccurate
            g_paused = 0;
            return;
        }

        double pause_duration = (now.tv_sec - g_pause_start.tv_sec)
                              + (now.tv_nsec - g_pause_start.tv_nsec) / 1e9;
        g_total_pause_offset += pause_duration;
        g_paused = 0;

        log_info("TIME_SERVER", "Resumed after %.2f seconds pause (total offset: %.2f)",
                 pause_duration, g_total_pause_offset);
    }
}

/**
 * @brief Update the atomic simulated time in SharedState
 */
static void update_sim_time(SharedState *state) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        // Don't spam errors on every tick - just skip this update
        return;
    }

    // Calculate effective elapsed time (excluding pauses)
    double now_sec = now.tv_sec + now.tv_nsec / 1e9;
    double start_sec = g_real_start_time.tv_sec + g_real_start_time.tv_nsec / 1e9;

    double effective_elapsed = now_sec - start_sec - g_total_pause_offset;

    // If currently paused, don't count time since pause started
    if (g_paused) {
        double current_pause = (now.tv_sec - g_pause_start.tv_sec)
                             + (now.tv_nsec - g_pause_start.tv_nsec) / 1e9;
        effective_elapsed -= current_pause;
    }

    if (effective_elapsed < 0) effective_elapsed = 0;

    // Convert to simulated time
    double sim_minutes = state->sim_start_minutes + effective_elapsed * state->time_acceleration;
    int64_t sim_ms = (int64_t)(sim_minutes * 60.0 * 1000.0);

    // Atomic store for other processes to read
    __atomic_store_n(&state->current_sim_time_ms, sim_ms, __ATOMIC_RELEASE);
}

/**
 * @brief Time Server main entry point
 *
 * @param res IPC resources
 * @param keys IPC keys (unused)
 */
void time_server_main(IPCResources *res, IPCKeys *keys) {
    (void)keys;
    SharedState *state = res->state;

    // Initialize logger
    logger_init(state, LOG_TIME_SERVER);
    logger_set_debug_enabled(state->debug_logs_enabled);

    log_info("TIME_SERVER", "Time Server started (PID %d)", getpid());

    // Capture start time with high precision
    if (clock_gettime(CLOCK_MONOTONIC, &g_real_start_time) == -1) {
        perror("time_server: clock_gettime for start time");
        log_error("TIME_SERVER", "Failed to get start time, exiting");
        return;
    }

    // Install signal handlers
    struct sigaction sa;

    // SIGTSTP: Use SA_RESETHAND so handler resets to SIG_DFL before running.
    // This allows raise(SIGTSTP) to actually suspend the process.
    // We save the action in g_sigtstp_action so SIGCONT can reinstall it.
    g_sigtstp_action.sa_handler = sigtstp_handler;
    sigemptyset(&g_sigtstp_action.sa_mask);
    g_sigtstp_action.sa_flags = SA_RESETHAND;
    sigaction(SIGTSTP, &g_sigtstp_action, NULL);

    sa.sa_handler = sigcont_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGCONT, &sa, NULL);

    sa.sa_handler = sigterm_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = sigalrm_handler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);

    // Set up periodic timer (every 10ms = 100 updates/sec)
    struct itimerval timer = {
        .it_interval = { .tv_sec = 0, .tv_usec = 10000 },
        .it_value = { .tv_sec = 0, .tv_usec = 10000 }
    };
    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
        perror("time_server: setitimer");
        return;
    }

    log_debug("TIME_SERVER", "Timer started (10ms interval)");

    // Signal that this worker is ready (startup barrier)
    ipc_signal_worker_ready(res);
    log_info("TIME_SERVER", "Time Server ready");

    // Initial time update
    update_sim_time(state);

    // Main loop
    while (g_running && state->running) {
        // Handle resume if we got SIGCONT
        handle_resume();

        // Update simulated time
        update_sim_time(state);

        // Wait for next timer tick or signal
        pause();
    }

    // Disable timer
    timer.it_interval.tv_usec = 0;
    timer.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);

    log_debug("TIME_SERVER", "Time Server exiting");
}
