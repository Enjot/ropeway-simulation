#pragma once

/**
 * @file lifecycle/zombie_reaper.h
 * @brief Zombie process reaping and worker wait functions.
 */

/**
 * @brief Reap zombie child processes (non-blocking).
 *
 * Should be called in main loop when g_child_exited is set.
 */
void reap_zombies(void);

/**
 * @brief Wait for all worker processes to exit (blocking).
 *
 * Called during shutdown after signaling workers.
 */
void wait_for_workers(void);
