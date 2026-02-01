#include "types.h"
#include "config.h"
#include "logger.h"
#include "time_sim.h"
#include "ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <errno.h>
#include <time.h>

// Forward declarations for worker entry points
void cashier_main(IPCResources *res, IPCKeys *keys);
void lower_worker_main(IPCResources *res, IPCKeys *keys);
void upper_worker_main(IPCResources *res, IPCKeys *keys);
void tourist_generator_main(IPCResources *res, IPCKeys *keys, const char *tourist_exe);

// Global for signal handlers
static IPCResources g_res;
static int g_running = 1;
static int g_child_exited = 0;

// Issue #3, #5 fix: Signal flags instead of direct shared state modification
static volatile sig_atomic_t g_sigtstp_received = 0;
static volatile sig_atomic_t g_sigcont_received = 0;

// Signal-safe flag setting
static void sigchld_handler(int sig) {
    (void)sig;
    g_child_exited = 1;
}

static void sigterm_handler(int sig) {
    (void)sig;
    g_running = 0;
    write(STDERR_FILENO, "[SIGNAL] Shutdown requested\n", 28);
}

// Forward declaration for reinstallation in sigcont_handler
static void sigtstp_handler(int sig);

// Issue #3, #5 fix: Signal handler sets flag, then actually stops the process
// We reset to SIG_DFL and re-raise SIGTSTP so the process actually suspends
static void sigtstp_handler(int sig) {
    (void)sig;
    g_sigtstp_received = 1;
    write(STDERR_FILENO, "[SIGNAL] SIGTSTP received\n", 26);

    // Reset SIGTSTP to default handler and re-raise to actually stop
    signal(SIGTSTP, SIG_DFL);
    raise(SIGTSTP);
}

static void sigcont_handler(int sig) {
    (void)sig;
    g_sigcont_received = 1;
    write(STDERR_FILENO, "[SIGNAL] SIGCONT received\n", 26);

    // Reinstall our custom SIGTSTP handler (was reset to SIG_DFL before stopping)
    signal(SIGTSTP, sigtstp_handler);
}

// SIGALRM handler - just wakes up from pause() to check simulation time
static void sigalrm_handler(int sig) {
    (void)sig;
    // Nothing to do - just wake up from pause()
}

// Issue #3 fix: Handle pause state changes outside signal handler (safe IPC)
static void handle_pause_signal(void) {
    if (!g_sigtstp_received) return;
    g_sigtstp_received = 0;

    if (g_res.state) {
        // Protected access to shared state
        if (sem_wait(g_res.sem_id, SEM_STATE, 1) == -1) {
            return;  // Shutdown in progress
        }
        g_res.state->paused = 1;
        g_res.state->pause_start_time = time(NULL);
        sem_post(g_res.sem_id, SEM_STATE, 1);
    }
    write(STDERR_FILENO, "[MAIN] Simulation paused\n", 25);
}

// Issue #3, #7 fix: Handle resume with proper pause waiter tracking
static void handle_resume_signal(void) {
    if (!g_sigcont_received) return;
    g_sigcont_received = 0;

    if (g_res.state) {
        if (sem_wait(g_res.sem_id, SEM_STATE, 1) == -1) {
            return;  // Shutdown in progress
        }
        if (g_res.state->paused) {
            time_t pause_duration = time(NULL) - g_res.state->pause_start_time;
            g_res.state->total_pause_offset += pause_duration;
            g_res.state->pause_start_time = 0;
            g_res.state->paused = 0;
        }
        sem_post(g_res.sem_id, SEM_STATE, 1);

        // Issue #7 fix: Release pause waiters using tracked count
        ipc_release_pause_waiters(&g_res);
    }
    write(STDERR_FILENO, "[MAIN] Simulation resumed\n", 26);
}

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
        log_info("MAIN", "%s process started (PID %d)", name, getpid());
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
    const char *type_names[] = {"Walker", "Cyclist"};

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
    fprintf(stderr, "Usage: %s [config_file]\n", prog);
    fprintf(stderr, "  config_file: Path to configuration file (default: config/default.conf)\n");
}

int main(int argc, char *argv[]) {
    const char *config_path = "config/default.conf";
    const char *tourist_exe = "./tourist";

    // Parse arguments
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        config_path = argv[1];
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

    // Create IPC resources
    if (ipc_create(&g_res, &keys, &cfg) == -1) {
        fprintf(stderr, "Error: Failed to create IPC resources\n");
        return 1;
    }

    // Initialize logger with shared state
    logger_init(g_res.state, LOG_MAIN);

    // Initialize time
    time_init(g_res.state, &cfg);

    // Store main PID
    g_res.state->main_pid = getpid();

    log_info("MAIN", "Simulation starting at %02d:%02d",
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

    sa.sa_handler = sigtstp_handler;
    sigaction(SIGTSTP, &sa, NULL);

    sa.sa_handler = sigcont_handler;
    sigaction(SIGCONT, &sa, NULL);

    sa.sa_handler = sigalrm_handler;
    sigaction(SIGALRM, &sa, NULL);

    // Spawn workers
    g_res.state->cashier_pid = spawn_worker(cashier_main, &g_res, &keys, "Cashier");
    g_res.state->lower_worker_pid = spawn_worker(lower_worker_main, &g_res, &keys, "LowerWorker");
    g_res.state->upper_worker_pid = spawn_worker(upper_worker_main, &g_res, &keys, "UpperWorker");
    g_res.state->generator_pid = spawn_generator(&g_res, &keys, tourist_exe);

    if (g_res.state->cashier_pid == -1 ||
        g_res.state->lower_worker_pid == -1 ||
        g_res.state->upper_worker_pid == -1 ||
        g_res.state->generator_pid == -1) {
        log_error("MAIN", "Failed to spawn one or more workers");
        g_res.state->running = 0;
    }

    log_info("MAIN", "All workers spawned, simulation running");

    // Main loop - handle signals and reap zombies
    while (g_running && g_res.state->running) {
        // Handle pause/resume signals (issue #3 fix - outside signal handler)
        handle_pause_signal();
        handle_resume_signal();

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
    if (g_res.state->cashier_pid > 0) kill(g_res.state->cashier_pid, SIGTERM);
    if (g_res.state->lower_worker_pid > 0) kill(g_res.state->lower_worker_pid, SIGTERM);
    if (g_res.state->upper_worker_pid > 0) kill(g_res.state->upper_worker_pid, SIGTERM);
    if (g_res.state->generator_pid > 0) kill(g_res.state->generator_pid, SIGTERM);

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
