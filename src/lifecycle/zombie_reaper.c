/**
 * @file lifecycle/zombie_reaper.c
 * @brief Zombie process reaping and worker wait functions.
 */

#include "lifecycle/zombie_reaper.h"
#include "lifecycle/process_signals.h"

#include <errno.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

/**
 * @brief Reap any zombie child processes (non-blocking).
 *
 * Called from main loop when SIGCHLD indicates a child has exited.
 */
void reap_zombies(void) {
    if (!g_child_exited) return;
    g_child_exited = 0;

    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "[DEBUG] [REAPER] Reaped PID %d\n", (int)pid);
        if (len > 0 && len < (int)sizeof(buf)) {
            write(STDERR_FILENO, buf, len);
        }
    }
}

/**
 * @brief Wait for all worker processes to exit (blocking).
 *
 * Called during shutdown to ensure all children have terminated.
 */
void wait_for_workers(void) {
    int status;
    pid_t pid;

    while (1) {
        pid = waitpid(-1, &status, 0);
        if (pid > 0) {
            char buf[64];
            int len = snprintf(buf, sizeof(buf), "[DEBUG] [MAIN] Worker exited: PID %d\n", (int)pid);
            if (len > 0 && len < (int)sizeof(buf)) {
                write(STDERR_FILENO, buf, len);
            }
            continue;
        }
        if (pid == -1 && errno == EINTR) {
            // Signal interrupted waitpid, retry
            continue;
        }
        // ECHILD or other error - no more children
        break;
    }

    if (errno != ECHILD && errno != EINTR) {
        perror("wait_for_workers: waitpid");
    }
}
