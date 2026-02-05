#define _GNU_SOURCE

/**
 * @file lifecycle/process_signals.c
 * @brief Signal handling for main process.
 */

#include "lifecycle/process_signals.h"

#include <signal.h>
#include <unistd.h>

// Global signal flags
int g_running = 1;
int g_child_exited = 0;
pid_t g_main_pid = 0;

// Pointer to IPC resources for signal-safe cleanup
static IPCResources *g_res_ptr = NULL;

/**
 * @brief SIGCHLD handler - sets flag for zombie reaping.
 *
 * @param sig Signal number (unused).
 */
static void sigchld_handler(int sig) {
    (void)sig;
    g_child_exited = 1;
}

/**
 * @brief SIGTERM/SIGINT handler - triggers shutdown.
 *
 * Only main process performs IPC cleanup.
 *
 * @param sig Signal number (unused).
 */
static void sigterm_handler(int sig) {
    (void)sig;
    g_running = 0;

    // Only main process should clean up IPC resources.
    // Child processes inherit this handler but g_main_pid
    // remains set to the main process's PID.
    if (g_main_pid == 0 || getpid() != g_main_pid) {
        write(STDERR_FILENO, "[SIGNAL] Child shutdown\n", 24);
        return;
    }

    // Clean up IPC resources directly in signal handler (async-signal-safe)
    // This ensures cleanup even if main loop is stuck
    if (g_res_ptr) {
        ipc_cleanup_signal_safe(g_res_ptr);
    }

    write(STDERR_FILENO, "[SIGNAL] Shutdown requested, IPC cleaned\n", 41);
}

/**
 * @brief SIGALRM handler - wakes up from pause() to check simulation time.
 *
 * @param sig Signal number (unused).
 */
static void sigalrm_handler(int sig) {
    (void)sig;
    // Nothing to do - just wake up from pause()
}

/**
 * @brief Initialize signal handling module.
 *
 * Must be called before install_signal_handlers().
 *
 * @param res Pointer to IPC resources (for signal-safe cleanup).
 */
void signals_init(IPCResources *res) {
    g_res_ptr = res;
}

/**
 * @brief Install all signal handlers for main process.
 *
 * Sets up SIGCHLD, SIGINT, SIGTERM, SIGALRM handlers.
 */
void install_signal_handlers(void) {
    struct sigaction sa;

    // SIGCHLD - child process status change
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    // SIGINT/SIGTERM - shutdown request
    sa.sa_handler = sigterm_handler;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Note: SIGTSTP/SIGCONT are handled by Time Server for pause offset tracking.
    // Main process uses default handling (kernel suspends us).

    // SIGALRM - timer for checking simulation end
    sa.sa_handler = sigalrm_handler;
    sigaction(SIGALRM, &sa, NULL);
}
