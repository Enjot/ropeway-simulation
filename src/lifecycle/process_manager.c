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
