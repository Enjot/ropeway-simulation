#define _GNU_SOURCE

/**
 * @file main.c
 * @brief Main process orchestrator for ropeway simulation.
 */

#include "constants.h"
#include "core/config.h"
#include "core/logger.h"
#include "core/time_sim.h"
#include "core/report.h"
#include "ipc/ipc.h"
#include "lifecycle/process_signals.h"
#include "lifecycle/process_manager.h"
#include "lifecycle/zombie_reaper.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <unistd.h>

// Forward declarations for worker entry points
void time_server_main(IPCResources *res, IPCKeys *keys);
void cashier_main(IPCResources *res, IPCKeys *keys);
void lower_worker_main(IPCResources *res, IPCKeys *keys);
void upper_worker_main(IPCResources *res, IPCKeys *keys);

// Global IPC resources (used by signal handler via signals_init)
static IPCResources g_res;

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [config_path]\n", prog);
    fprintf(stderr, "  config_path: Config file path or name (default: default.conf)\n");
    fprintf(stderr, "               If just a filename, looks in ../config/\n");
}

/**
 * Signal workers to stop and destroy IPC to unblock blocked operations.
 */
static void shutdown_workers(void) {
    // Send SIGTERM to worker processes
    // Note: ESRCH (no such process) is expected if process already exited
    if (g_res.state->time_server_pid > 0) {
        if (kill(g_res.state->time_server_pid, SIGTERM) == -1 && errno != ESRCH) {
            perror("main: kill time_server");
        }
    }
    if (g_res.state->cashier_pid > 0) {
        if (kill(g_res.state->cashier_pid, SIGTERM) == -1 && errno != ESRCH) {
            perror("main: kill cashier");
        }
    }
    if (g_res.state->lower_worker_pid > 0) {
        if (kill(g_res.state->lower_worker_pid, SIGTERM) == -1 && errno != ESRCH) {
            perror("main: kill lower_worker");
        }
    }
    if (g_res.state->upper_worker_pid > 0) {
        if (kill(g_res.state->upper_worker_pid, SIGTERM) == -1 && errno != ESRCH) {
            perror("main: kill upper_worker");
        }
    }
    if (g_res.state->generator_pid > 0) {
        if (kill(g_res.state->generator_pid, SIGTERM) == -1 && errno != ESRCH) {
            perror("main: kill generator");
        }
    }

    // Destroy message queues to unblock any stuck msgrcv/msgsnd operations
    if (g_res.mq_cashier_id != -1) {
        msgctl(g_res.mq_cashier_id, IPC_RMID, NULL);
        g_res.mq_cashier_id = -1;
    }
    if (g_res.mq_platform_id != -1) {
        msgctl(g_res.mq_platform_id, IPC_RMID, NULL);
        g_res.mq_platform_id = -1;
    }
    if (g_res.mq_boarding_id != -1) {
        msgctl(g_res.mq_boarding_id, IPC_RMID, NULL);
        g_res.mq_boarding_id = -1;
    }
    if (g_res.mq_arrivals_id != -1) {
        msgctl(g_res.mq_arrivals_id, IPC_RMID, NULL);
        g_res.mq_arrivals_id = -1;
    }
    if (g_res.mq_worker_id != -1) {
        msgctl(g_res.mq_worker_id, IPC_RMID, NULL);
        g_res.mq_worker_id = -1;
    }

    // Destroy semaphores to unblock any stuck semop operations
    if (g_res.sem_id != -1) {
        semctl(g_res.sem_id, 0, IPC_RMID);
        g_res.sem_id = -1;
    }
}

int main(int argc, char *argv[]) {
    static char config_path[256];
    const char *config_name = "default.conf";
#ifndef TOURIST_EXE_PATH
#error "TOURIST_EXE_PATH must be defined by CMake"
#endif
    const char *tourist_exe = TOURIST_EXE_PATH;

    // Parse arguments
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        config_name = argv[1];
    }

    // Build config path
    // If config_name is an absolute path or contains a directory separator,
    // use it directly; otherwise, look in ../config/
    if (config_name[0] == '/' || strchr(config_name, '/') != NULL) {
        snprintf(config_path, sizeof(config_path), "%s", config_name);
    } else {
        snprintf(config_path, sizeof(config_path), "../config/%s", config_name);
    }

    // Check tourist executable exists
    if (access(tourist_exe, X_OK) == -1) {
        fprintf(stderr, "Error: tourist executable not found at %s\n", tourist_exe);
        fprintf(stderr, "Make sure to build the tourist executable first.\n");
        return 1;
    }

    // Load configuration
    Config cfg;
    if (config_load(config_path, &cfg) == -1) {
        fprintf(stderr, "Error: Failed to load config from %s\n", config_path);
        return 1;
    }

    if (config_validate(&cfg) == -1) {
        fprintf(stderr, "Error: Invalid configuration\n");
        return 1;
    }

    printf("Ropeway Simulation Starting\n");
    printf("Config: %s\n", config_path);
    printf("Station capacity: %d\n", cfg.station_capacity);
    printf("Simulation: %02d:%02d - %02d:%02d (real time: %d seconds)\n",
           cfg.sim_start_hour, cfg.sim_start_minute,
           cfg.sim_end_hour, cfg.sim_end_minute,
           cfg.simulation_duration_real);

    // Generate IPC keys
    IPCKeys keys;
    if (ipc_generate_keys(&keys, ".") == -1) {
        fprintf(stderr, "Error: Failed to generate IPC keys\n");
        return 1;
    }

    // Clean up stale IPC resources from previous crashed run
    ipc_cleanup_stale(&keys);

    // Create IPC resources
    if (ipc_create(&g_res, &keys, &cfg) == -1) {
        fprintf(stderr, "Error: Failed to create IPC resources\n");
        return 1;
    }

    // Initialize logger with shared state
    logger_init(g_res.state, LOG_MAIN);
    logger_set_debug_enabled(cfg.debug_logs_enabled);

    // Initialize time
    time_init(g_res.state, &cfg);

    // Store main PID
    g_main_pid = getpid();
    g_res.state->main_pid = g_main_pid;

    log_debug("MAIN", "Simulation starting at %02d:%02d",
             cfg.sim_start_hour, cfg.sim_start_minute);

    // Initialize and install signal handlers
    signals_init(&g_res);
    install_signal_handlers();

    // Spawn Time Server first (handles time tracking and pause offset)
    g_res.state->time_server_pid = spawn_worker(time_server_main, &g_res, &keys, "TimeServer");

    // Spawn other workers
    g_res.state->cashier_pid = spawn_worker(cashier_main, &g_res, &keys, "Cashier");
    g_res.state->lower_worker_pid = spawn_worker(lower_worker_main, &g_res, &keys, "LowerWorker");
    g_res.state->upper_worker_pid = spawn_worker(upper_worker_main, &g_res, &keys, "UpperWorker");

    if (g_res.state->time_server_pid == -1 ||
        g_res.state->cashier_pid == -1 ||
        g_res.state->lower_worker_pid == -1 ||
        g_res.state->upper_worker_pid == -1) {
        log_error("MAIN", "Failed to spawn one or more workers");
        g_res.state->running = 0;
    }

    // Wait for all workers to be ready before starting tourist generator
    if (g_res.state->running) {
        if (ipc_wait_workers_ready(&g_res, WORKER_COUNT_FOR_BARRIER) == -1) {
            log_error("MAIN", "Failed to wait for workers to be ready");
            g_res.state->running = 0;
        }
    }

    // Now spawn the tourist generator (workers are guaranteed to be ready)
    if (g_res.state->running) {
        g_res.state->generator_pid = spawn_generator(&g_res, &keys, tourist_exe);
        if (g_res.state->generator_pid == -1) {
            log_error("MAIN", "Failed to spawn tourist generator");
            g_res.state->running = 0;
        }
    }

    log_debug("MAIN", "All workers spawned, simulation running");

    // Main loop - handle signals and reap zombies
    while (g_running && g_res.state->running) {
        reap_zombies();

        // Check if simulation time is over
        if (time_is_simulation_over(g_res.state)) {
            log_info("MAIN", "Simulation time ended");
            g_res.state->closing = 1;
            break;
        }

        // Set alarm to wake up in 1 second to check time
        alarm(1);

        // Block until next signal (SIGCHLD, SIGALRM, etc.)
        pause();
    }

    log_info("MAIN", "Shutting down simulation");

    // Signal all processes to stop
    g_res.state->running = 0;

    // Signal workers and destroy IPC to unblock them
    shutdown_workers();

    // Wait for workers to exit
    wait_for_workers();

    // Print report - shared memory is still attached
    // TODO remove print_report(g_res.state);

    // Write report to file
    if (write_report_to_file(g_res.state, "simulation_report.txt") == 0) {
        write(STDERR_FILENO, "[INFO] [MAIN] Report saved to simulation_report.txt\n", 52);
    }

    // Cleanup remaining IPC resources (shared memory)
    ipc_destroy(&g_res);

    write(STDERR_FILENO, "[INFO] [MAIN] Simulation ended\n", 31);

    return 0;
}
