#include "types.h"
#include "ipc.h"
#include "logger.h"
#include "time_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>

static int g_running = 1;
static int g_child_exited = 0;

// Track child PIDs for cleanup (match max_tracked_tourists default)
#define MAX_CHILD_PIDS 5000
static pid_t g_child_pids[MAX_CHILD_PIDS];
static int g_child_count = 0;

static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_running = 0;
    } else if (sig == SIGCHLD) {
        g_child_exited = 1;
    }
}

/**
 * Reap zombie child processes (non-blocking).
 */
static int reap_zombies(void) {
    if (!g_child_exited) return 0;
    g_child_exited = 0;

    int reaped = 0;
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        reaped++;
    }

    return reaped;
}

/**
 * Generate random age (8-80) and check if person can have kids.
 * Adults 26+ can be guardians for kids aged 4-7.
 */
static int generate_age(int *can_have_kids) {
    int r = rand() % 100;

    if (r < 5) {
        *can_have_kids = 1;  // Seniors 65+ can be guardians
        return 65 + (rand() % 16);  // 5% seniors (65-80)
    } else if (r < 15) {
        *can_have_kids = 0;  // Young (8-17) cannot have kids
        return 8 + (rand() % 10);   // 10% young (8-17)
    } else if (r < 30) {
        *can_have_kids = 0;  // Young adults 18-25 cannot have kids
        return 18 + (rand() % 8);   // 15% young adults (18-25)
    } else {
        *can_have_kids = 1;  // Adults 26+ can be guardians
        return 26 + (rand() % 39);  // 70% adults (26-64)
    }
}

/**
 * Generate number of kids for an adult walker (0-2).
 * ~60% no kids, ~25% one kid, ~15% two kids.
 */
static int generate_kid_count(void) {
    int r = rand() % 100;
    if (r < 60) return 0;  // 60% no kids
    if (r < 85) return 1;  // 25% one kid
    return 2;              // 15% two kids
}


// Generate tourist type (walker or cyclist)
static TouristType generate_type(SharedState *state) {
    return (rand() % 100) < state->walker_percentage ? TOURIST_WALKER : TOURIST_CYCLIST;
}

// Check if tourist is VIP
static int is_vip(SharedState *state) {
    return (rand() % 100) < state->vip_percentage;
}

// Check pause state
static void check_pause(IPCResources *res) {
    if (sem_wait(res->sem_id, SEM_STATE, 1) == -1) {
        return;  // Shutdown in progress
    }
    int paused = res->state->paused;
    sem_post(res->sem_id, SEM_STATE, 1);

    if (paused) {
        log_debug("GENERATOR", "Paused, waiting for resume...");
        sem_wait(res->sem_id, SEM_PAUSE, 1);  // May fail on shutdown, OK
        log_debug("GENERATOR", "Resumed");
    }
}

void tourist_generator_main(IPCResources *res, IPCKeys *keys, const char *tourist_exe) {
    (void)keys;

    // Initialize logger with component type
    logger_init(res->state, LOG_GENERATOR);

    // Install signal handlers
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    // Seed random number generator
    srand(time(NULL) ^ getpid());

    log_info("GENERATOR", "Tourist generator started (rate: %d/sec, max: %d)",
             res->state->tourist_spawn_rate, res->state->max_concurrent_tourists);

    int tourist_id = 0;
    int active_tourists = 0;

    // Calculate spawn interval in microseconds
    int spawn_interval_us = 1000000 / res->state->tourist_spawn_rate;

    while (g_running && res->state->running) {
        // Reap zombie children and update active count
        int reaped = reap_zombies();
        active_tourists -= reaped;
        if (active_tourists < 0) active_tourists = 0;

        // Check pause
        check_pause(res);

        // Check if closing
        if (res->state->closing) {
            log_info("GENERATOR", "Station closing, stopping tourist generation");
            break;
        }

        // Check if we've reached max concurrent tourists
        if (active_tourists >= res->state->max_concurrent_tourists) {
            usleep(spawn_interval_us);
            continue;
        }

        // Generate tourist attributes
        tourist_id++;
        int can_have_kids = 0;
        int age = generate_age(&can_have_kids);
        TouristType type = generate_type(res->state);
        int vip = is_vip(res->state);

        // Determine number of kids (only walkers 26+ can have kids)
        int kid_count = 0;
        if (type == TOURIST_WALKER && can_have_kids && age >= 26) {
            kid_count = generate_kid_count();
        }

        // Prepare arguments for exec
        char id_str[16];
        char age_str[8];
        char type_str[2];
        char vip_str[2];
        char kid_count_str[8];

        snprintf(id_str, sizeof(id_str), "%d", tourist_id);
        snprintf(age_str, sizeof(age_str), "%d", age);
        snprintf(type_str, sizeof(type_str), "%d", type);
        snprintf(vip_str, sizeof(vip_str), "%d", vip);
        snprintf(kid_count_str, sizeof(kid_count_str), "%d", kid_count);

        // Fork and exec tourist process
        pid_t pid = fork();

        if (pid == -1) {
            perror("generator: fork");
            usleep(spawn_interval_us);
            continue;
        }

        if (pid == 0) {
            // Child process - exec tourist
            execl(tourist_exe, "tourist", id_str, age_str, type_str, vip_str,
                  kid_count_str, NULL);

            // If exec fails
            perror("generator: execl");
            _exit(1);
        }

        // Parent process - track PID for cleanup
        active_tourists++;
        if (g_child_count < MAX_CHILD_PIDS) {
            g_child_pids[g_child_count++] = pid;
        }

        const char *type_name = type == TOURIST_WALKER ? "walker" : "cyclist";
        if (kid_count > 0) {
            log_debug("GENERATOR", "Spawned tourist %d: age=%d, type=%s, vip=%s, kids=%d (PID %d)",
                     tourist_id, age, type_name, vip ? "yes" : "no", kid_count, pid);
        } else {
            log_debug("GENERATOR", "Spawned tourist %d: age=%d, type=%s, vip=%s (PID %d)",
                     tourist_id, age, type_name, vip ? "yes" : "no", pid);
        }

        // Sleep before next spawn
        usleep(spawn_interval_us);
    }

    log_info("GENERATOR", "Tourist generator shutting down (spawned %d tourists)", tourist_id);

    // Send SIGTERM to all tracked tourist processes
    log_debug("GENERATOR", "Sending SIGTERM to %d tourists...", g_child_count);
    for (int i = 0; i < g_child_count; i++) {
        if (g_child_pids[i] > 0) {
            kill(g_child_pids[i], SIGTERM);
        }
    }

    // Wait for tourists to exit with non-blocking waitpid first
    log_debug("GENERATOR", "Waiting for %d tourists to exit...", active_tourists);

    int status;
    pid_t pid;
    int wait_cycles = 0;
    const int max_wait_cycles = 10;  // ~1 second total

    // Non-blocking wait loop to allow graceful exit
    while (active_tourists > 0 && wait_cycles < max_wait_cycles) {
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            active_tourists--;
            log_debug("GENERATOR", "Tourist exited (PID %d), %d remaining", (int)pid, active_tourists);
        }
        if (active_tourists > 0) {
            usleep(100000);  // 100ms
            wait_cycles++;
        }
    }

    // If still have remaining tourists, force kill them individually
    if (active_tourists > 0) {
        log_debug("GENERATOR", "Force killing %d remaining tourists...", active_tourists);
        for (int i = 0; i < g_child_count; i++) {
            if (g_child_pids[i] > 0) {
                kill(g_child_pids[i], SIGKILL);
            }
        }

        // Reap the killed processes
        while ((pid = waitpid(-1, &status, 0)) > 0) {
            active_tourists--;
            log_debug("GENERATOR", "Tourist killed (PID %d), %d remaining", (int)pid, active_tourists);
        }
    }

    log_debug("GENERATOR", "All tourists exited");
}
