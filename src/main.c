#include "constants.h"
#include "config.h"
#include "logger.h"
#include "time_sim.h"
#include "ipc/ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <errno.h>
#include <time.h>

// Forward declarations for worker entry points
void time_server_main(IPCResources *res, IPCKeys *keys);
void cashier_main(IPCResources *res, IPCKeys *keys);
void lower_worker_main(IPCResources *res, IPCKeys *keys);
void upper_worker_main(IPCResources *res, IPCKeys *keys);
void tourist_generator_main(IPCResources *res, IPCKeys *keys, const char *tourist_exe);

// Global for signal handlers
static IPCResources g_res;
static int g_running = 1;
static int g_child_exited = 0;
static pid_t g_main_pid = 0;  // Set in main() to identify main process

// Signal-safe flag setting
static void sigchld_handler(int sig) {
    (void)sig;
    g_child_exited = 1;
}

static void sigterm_handler(int sig) {
    (void)sig;
    g_running = 0;

    // Only main process should clean up IPC resources.
    // Note: getpid() is async-signal-safe per POSIX.1-2008, but we use
    // the cached g_main_pid comparison which is more efficient and avoids
    // any syscall. Child processes inherit this handler but g_main_pid
    // remains set to the main process's PID.
    if (g_main_pid == 0 || getpid() != g_main_pid) {
        write(STDERR_FILENO, "[SIGNAL] Child shutdown\n", 24);
        return;
    }

    // Clean up IPC resources directly in signal handler (async-signal-safe)
    // This ensures cleanup even if main loop is stuck
    ipc_cleanup_signal_safe(&g_res);

    write(STDERR_FILENO, "[SIGNAL] Shutdown requested, IPC cleaned\n", 41);
}

// SIGALRM handler - just wakes up from pause() to check simulation time
static void sigalrm_handler(int sig) {
    (void)sig;
    // Nothing to do - just wake up from pause()
}

// Note: SIGTSTP/SIGCONT pause handling is now done by the Time Server process.
// The kernel handles stopping/resuming all processes automatically.

// Reap zombie processes
// Issue #15 fix: Use snprintf instead of strcat for safety
static void reap_zombies(void) {
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

// Spawn a worker process
static pid_t spawn_worker(void (*worker_func)(IPCResources*, IPCKeys*),
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

// Spawn tourist generator (needs tourist executable path)
static pid_t spawn_generator(IPCResources *res, IPCKeys *keys, const char *tourist_exe) {
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

// Wait for all workers to exit
// Issue #15 fix: Use snprintf instead of strcat for safety
static void wait_for_workers(void) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, 0)) > 0) {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "[DEBUG] [MAIN] Worker exited: PID %d\n", (int)pid);
        if (len > 0 && len < (int)sizeof(buf)) {
            write(STDERR_FILENO, buf, len);
        }
    }

    if (errno != ECHILD) {
        perror("wait_for_workers: waitpid");
    }
}

// Print final report
static void print_report(SharedState *state) {
    printf("\n========== SIMULATION REPORT ==========\n");

    char start_buf[8], end_buf[8];
    time_format_minutes(state->sim_start_minutes, start_buf, sizeof(start_buf));
    time_format_minutes(state->sim_end_minutes, end_buf, sizeof(end_buf));

    printf("Duration: %s - %s (simulated)\n", start_buf, end_buf);
    printf("Total tourists: %d\n", state->total_tourists);
    printf("Total rides: %d\n\n", state->total_rides);

    // Per-tourist summary
    printf("--- Per-Tourist Summary ---\n");
    printf("%-6s %-8s %-10s %-8s %-6s %s\n",
           "ID", "Type", "Ticket", "Entry", "Rides", "Notes");
    printf("----------------------------------------------\n");

    const char *ticket_names[] = {"SINGLE", "TIME_T1", "TIME_T2", "TIME_T3", "DAILY"};
    const char *type_names[] = {"Walker", "Cyclist", "Family"};

    for (int i = 0; i < state->tourist_entry_count && i < state->max_tracked_tourists; i++) {
        TouristEntry *e = &state->tourist_entries[i];
        if (!e->active) continue;

        char entry_time_buf[8];
        time_format_minutes(e->entry_time_sim, entry_time_buf, sizeof(entry_time_buf));

        char notes[32] = "";
        if (e->is_vip) strcat(notes, "VIP ");
        if (e->kid_count > 0) {
            char kid_note[16];
            snprintf(kid_note, sizeof(kid_note), "+%d kids", e->kid_count);
            strcat(notes, kid_note);
        }

        printf("%-6d %-8s %-10s %-8s %-6d %s\n",
               e->tourist_id,
               type_names[e->tourist_type],
               ticket_names[e->ticket_type],
               entry_time_buf,
               e->total_rides,
               notes);
    }

    // Aggregates by ticket type
    printf("\n--- Aggregates by Ticket Type ---\n");
    for (int i = 0; i < TICKET_COUNT; i++) {
        printf("  %-10s %5d tourists, %5d rides\n",
               ticket_names[i],
               state->tourists_by_ticket[i],
               state->rides_by_ticket[i]);
    }

    printf("\n=======================================\n");
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [config_name]\n", prog);
    fprintf(stderr, "  config_name: Config file in ../config/ (default: default.conf)\n");
}

int main(int argc, char *argv[]) {
    static char config_path[256];
    const char *config_name = "default.conf";
    const char *tourist_exe = "./tourist";

    // Parse arguments
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        config_name = argv[1];
    }

    // Build config path
    snprintf(config_path, sizeof(config_path), "../config/%s", config_name);

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

    // Store main PID (both in shared state and global for signal handler)
    g_main_pid = getpid();
    g_res.state->main_pid = g_main_pid;

    log_debug("MAIN", "Simulation starting at %02d:%02d",
             cfg.sim_start_hour, cfg.sim_start_minute);

    // Install signal handlers
    struct sigaction sa;

    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    sa.sa_handler = sigterm_handler;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Note: SIGTSTP/SIGCONT are handled by Time Server for pause offset tracking.
    // Main process uses default handling (kernel suspends us).

    sa.sa_handler = sigalrm_handler;
    sigaction(SIGALRM, &sa, NULL);

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
    // This prevents race conditions where tourists try to use IPC before workers are ready
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
    // Note: Pause handling is done by Time Server; main just gets suspended by kernel
    while (g_running && g_res.state->running) {
        // Reap zombies
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
    // Processes will get EIDRM and should exit gracefully
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
    // Processes will get EIDRM and should exit gracefully
    if (g_res.sem_id != -1) {
        semctl(g_res.sem_id, 0, IPC_RMID);
        g_res.sem_id = -1;
    }

    // Now wait for workers to exit (they should be unblocked now)
    // Generator waits for tourists before exiting, so this implicitly waits for all
    wait_for_workers();

    // Print report - shared memory is still attached
    print_report(g_res.state);

    // Cleanup remaining IPC resources (shared memory)
    ipc_destroy(&g_res);

    // Use write() directly since logger requires shared state which is now destroyed
    write(STDERR_FILENO, "[INFO] [MAIN] Simulation ended\n", 31);

    return 0;
}
