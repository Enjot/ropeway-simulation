#pragma once

/**
 * Common signal handler macros for worker processes.
 * These macros generate async-signal-safe signal handlers.
 */

#include <unistd.h>

/**
 * Define a basic signal handler that sets g_running = 0 on SIGTERM/SIGINT.
 * Requires: static int g_running = 1; declared in the file.
 *
 * @param handler_name Name for the generated handler function
 */
#define DEFINE_BASIC_SIGNAL_HANDLER(handler_name) \
    static void handler_name(int sig) { \
        if (sig == SIGTERM || sig == SIGINT) { \
            g_running = 0; \
        } \
    }

/**
 * Define an emergency-capable signal handler for lower/upper workers.
 * Handles SIGTERM, SIGINT, SIGUSR1 (emergency), SIGUSR2 (resume), SIGALRM (timeout).
 * Uses write() for signal-safe logging.
 *
 * Requires:
 *   static int g_running = 1;
 *   static int g_emergency_signal = 0;
 *   static int g_resume_signal = 0;
 *   static int g_alarm_signal = 0;
 *
 * @param handler_name Name for the generated handler function
 * @param worker_tag String literal for logging (e.g., "LOWER_WORKER")
 */
#define DEFINE_EMERGENCY_SIGNAL_HANDLER(handler_name, worker_tag) \
    static void handler_name(int sig) { \
        if (sig == SIGTERM || sig == SIGINT) { \
            g_running = 0; \
        } else if (sig == SIGUSR1) { \
            g_emergency_signal = 1; \
            write(STDERR_FILENO, "[SIGNAL] [" worker_tag "] Emergency stop (SIGUSR1)\n", \
                  sizeof("[SIGNAL] [" worker_tag "] Emergency stop (SIGUSR1)\n") - 1); \
        } else if (sig == SIGUSR2) { \
            g_resume_signal = 1; \
            write(STDERR_FILENO, "[SIGNAL] [" worker_tag "] Resume request (SIGUSR2)\n", \
                  sizeof("[SIGNAL] [" worker_tag "] Resume request (SIGUSR2)\n") - 1); \
        } else if (sig == SIGALRM) { \
            g_alarm_signal = 1; \
        } \
    }
