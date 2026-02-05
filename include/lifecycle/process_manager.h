#pragma once

/**
 * @file lifecycle/process_manager.h
 * @brief Worker process spawning functions.
 */

#include "ipc/ipc.h"

/**
 * @brief Spawn a worker process.
 *
 * @param worker_func Entry point function for the worker.
 * @param res IPC resources.
 * @param keys IPC keys.
 * @param name Process name for logging.
 * @return PID of spawned process, -1 on error.
 */
pid_t spawn_worker(void (*worker_func)(IPCResources*, IPCKeys*),
                   IPCResources *res, IPCKeys *keys,
                   const char *name);

/**
 * @brief Spawn tourist generator process.
 *
 * @param res IPC resources.
 * @param keys IPC keys.
 * @param tourist_exe Path to tourist executable.
 * @return PID of spawned process, -1 on error.
 */
pid_t spawn_generator(IPCResources *res, IPCKeys *keys, const char *tourist_exe);
