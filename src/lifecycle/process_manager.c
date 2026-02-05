/**
 * @file lifecycle/process_manager.c
 * @brief Worker process spawning.
 */

#include "lifecycle/process_manager.h"
#include "core/logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Forward declaration for tourist generator entry point
void tourist_generator_main(IPCResources *res, IPCKeys *keys, const char *tourist_exe);

/**
 * @brief Spawn a worker process via fork.
 *
 * @param worker_func Worker entry point function.
 * @param res IPC resources for the worker.
 * @param keys IPC keys for the worker.
 * @param name Worker name for logging.
 * @return Child PID on success, -1 on error.
 */
pid_t spawn_worker(void (*worker_func)(IPCResources*, IPCKeys*),
                   IPCResources *res, IPCKeys *keys,
                   const char *name) {
    pid_t pid = fork();

    if (pid == -1) {
        perror("spawn_worker: fork");
        return -1;
    }

    if (pid == 0) {
        // Child process
        log_debug("MAIN", "%s process started (PID %d)", name, getpid());
        worker_func(res, keys);
        exit(0);
    }

    log_debug("MAIN", "Spawned %s with PID %d", name, pid);
    return pid;
}

/**
 * @brief Spawn the tourist generator process via fork.
 *
 * @param res IPC resources for the generator.
 * @param keys IPC keys for the generator.
 * @param tourist_exe Path to the tourist executable.
 * @return Child PID on success, -1 on error.
 */
pid_t spawn_generator(IPCResources *res, IPCKeys *keys, const char *tourist_exe) {
    pid_t pid = fork();

    if (pid == -1) {
        perror("spawn_generator: fork");
        return -1;
    }

    if (pid == 0) {
        log_info("MAIN", "Tourist generator started (PID %d)", getpid());
        tourist_generator_main(res, keys, tourist_exe);
        exit(0);
    }

    log_debug("MAIN", "Spawned tourist generator with PID %d", pid);
    return pid;
}
