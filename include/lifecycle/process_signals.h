#pragma once

/**
 * @file lifecycle/process_signals.h
 * @brief Signal handling for main process (SIGCHLD, SIGTERM, SIGALRM).
 */

#include "ipc/ipc.h"

// Global signal flags (defined in process_signals.c)
extern int g_running;
extern int g_child_exited;
extern pid_t g_main_pid;

/**
 * @brief Initialize signal handling module.
 *
 * Must be called before install_signal_handlers().
 *
 * @param res Pointer to IPC resources (for signal-safe cleanup).
 */
void signals_init(IPCResources *res);

/**
 * @brief Install all signal handlers for main process.
 *
 * Sets up SIGCHLD, SIGINT, SIGTERM, SIGALRM handlers.
 */
void install_signal_handlers(void);
