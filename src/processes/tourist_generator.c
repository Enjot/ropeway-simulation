#define _GNU_SOURCE

#include "constants.h"
#include "ipc/ipc.h"
#include "core/logger.h"
#include "core/time_sim.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int g_running = 1;
static int g_alarm_signal = 0;

// Zombie reaper thread state
static pthread_t reaper_thread;
static volatile int reaper_running = 0;
static volatile int active_tourists = 0;

/**
 * @brief Signal handler for SIGTERM, SIGINT, and SIGALRM.
 *
 * @param sig Signal number received.
 */
static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_running = 0;
    } else if (sig == SIGALRM) {
        g_alarm_signal = 1;
    }
}

/**
 * @brief Zombie reaper thread function.
 *
 * Uses sigwait() to wait for SIGCHLD and reaps zombie child processes.
 */
static void *reaper_thread_func(void *arg) {
    (void)arg;

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);

    while (reaper_running) {
        int sig;
        int ret = sigwait(&set, &sig);

        if (ret != 0 || !reaper_running) {
            break;
        }

        // Reap all zombie children (multiple may have exited)
        pid_t pid;
        int status;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            __atomic_fetch_sub(&active_tourists, 1, __ATOMIC_SEQ_CST);
        }
    }

    return NULL;
}

/**
 * @brief Start the zombie reaper thread.
 *
 * @return 0 on success, -1 on error.
 */
static int start_reaper_thread(void) {
    reaper_running = 1;
    if (pthread_create(&reaper_thread, NULL, reaper_thread_func, NULL) != 0) {
        perror("generator: pthread_create reaper");
        reaper_running = 0;
        return -1;
    }
    return 0;
}

/**
 * @brief Stop the zombie reaper thread.
 */
static void stop_reaper_thread(void) {
    if (!reaper_running) {
        return;
    }

    reaper_running = 0;
    pthread_kill(reaper_thread, SIGCHLD);  // Wake up sigwait()
    pthread_join(reaper_thread, NULL);
}

/**
 * @brief Generate random age (8-80) and check if person can have kids.
 *
 * Adults 26+ can be guardians for kids aged 4-7.
 *
 * @param can_have_kids Output: 1 if person can be a guardian, 0 otherwise.
 * @return Generated age in years.
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
 * @brief Generate number of kids for a family (1-2).
 *
 * Distribution: ~63% one kid, ~37% two kids (based on original 25:15 ratio).
 * Only called when family_percentage check already passed.
 *
 * @return Number of children (1 or 2).
 */
static int generate_kid_count(void) {
    int r = rand() % 100;
    if (r < 63) return 1;  // 63% one kid
    return 2;              // 37% two kids
}


/**
 * @brief Generate tourist type based on walker/cyclist percentage config.
 *
 * @param state Shared state with walker_percentage setting.
 * @return TOURIST_WALKER or TOURIST_CYCLIST.
 */
static TouristType generate_type(SharedState *state) {
    return (rand() % 100) < state->walker_percentage ? TOURIST_WALKER : TOURIST_CYCLIST;
}

/**
 * @brief Select ticket type for a tourist.
 *
 * Distribution: 30% single, 20% T1, 20% T2, 15% T3, 15% daily.
 *
 * @return Random ticket type.
 */
static TicketType select_ticket_type(void) {
    int r = rand() % 100;
    if (r < 30) return TICKET_SINGLE;
    if (r < 50) return TICKET_TIME_T1;
    if (r < 70) return TICKET_TIME_T2;
    if (r < 85) return TICKET_TIME_T3;
    return TICKET_DAILY;
}

/**
 * @brief Check if tourist should be VIP based on percentage config.
 *
 * @param state Shared state with vip_percentage setting.
 * @return 1 if VIP, 0 otherwise.
 */
static int is_vip(SharedState *state) {
    return (rand() % 100) < state->vip_percentage;
}

/**
 * @brief Tourist generator process entry point.
 *
 * Spawns tourist processes with random attributes (age, type, VIP status,
 * ticket type, kids). Uses fork+exec to create tourist processes. Uses a
 * dedicated zombie reaper thread to handle SIGCHLD via sigwait().
 *
 * @param res IPC resources (shared memory for config values).
 * @param keys IPC keys (unused, kept for interface consistency).
 * @param tourist_exe Path to tourist executable.
 */
void tourist_generator_main(IPCResources *res, IPCKeys *keys, const char *tourist_exe) {
    (void)keys;

    // Initialize logger with component type
    logger_init(res->state, LOG_GENERATOR);
    logger_set_debug_enabled(res->state->debug_logs_enabled);

    // Block SIGCHLD - will be handled by reaper thread via sigwait()
    sigset_t sigchld_mask;
    sigemptyset(&sigchld_mask);
    sigaddset(&sigchld_mask, SIGCHLD);
    pthread_sigmask(SIG_BLOCK, &sigchld_mask, NULL);

    // Start zombie reaper thread
    if (start_reaper_thread() != 0) {
        log_error("GENERATOR", "Failed to start reaper thread");
        return;
    }

    // Install signal handlers (not SIGCHLD - handled by thread)
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGALRM, &sa, NULL);

    // Seed random number generator
    srand(time(NULL) ^ getpid());

    log_info("GENERATOR", "Tourist generator started (total: %d, delay: %d us)",
             res->state->tourists_to_generate, res->state->tourist_spawn_delay_us);

    int tourist_id = 0;
    int total_to_spawn = res->state->tourists_to_generate;
    int spawn_delay_us = res->state->tourist_spawn_delay_us;

    while (g_running && res->state->running && tourist_id < total_to_spawn) {
        // Check if closing
        if (res->state->closing) {
            log_info("GENERATOR", "Station closing, stopping tourist generation");
            break;
        }

        // Generate tourist attributes
        tourist_id++;
        int can_have_kids = 0;
        int age = generate_age(&can_have_kids);
        TouristType type = generate_type(res->state);
        int vip = is_vip(res->state);
        TicketType ticket = select_ticket_type();

        // Determine if this walker becomes a family (only walkers 26+ can have kids)
        int kid_count = 0;
        if (type == TOURIST_WALKER && can_have_kids && age >= 26) {
            // Use family_percentage to decide if walker becomes a family
            if ((rand() % 100) < res->state->family_percentage) {
                kid_count = generate_kid_count();
                type = TOURIST_FAMILY;
            }
        }

        // Prepare arguments for exec
        char id_str[16];
        char age_str[8];
        char type_str[2];
        char vip_str[2];
        char kid_count_str[8];
        char ticket_str[2];

        snprintf(id_str, sizeof(id_str), "%d", tourist_id);
        snprintf(age_str, sizeof(age_str), "%d", age);
        snprintf(type_str, sizeof(type_str), "%d", type);
        snprintf(vip_str, sizeof(vip_str), "%d", vip);
        snprintf(kid_count_str, sizeof(kid_count_str), "%d", kid_count);
        snprintf(ticket_str, sizeof(ticket_str), "%d", ticket);

        // Fork and exec tourist process
        pid_t pid = fork();

        if (pid == -1) {
            perror("generator: fork");
            if (spawn_delay_us > 0) {
                usleep(spawn_delay_us);
            }
            continue;
        }

        if (pid == 0) {
            // Child process - stays in foreground process group for SIGTSTP
            // exec tourist
            execl(tourist_exe, "tourist", id_str, age_str, type_str, vip_str,
                  kid_count_str, ticket_str, NULL);

            // If exec fails
            perror("generator: execl");
            _exit(1);
        }

        // Parent process - increment active count
        __atomic_fetch_add(&active_tourists, 1, __ATOMIC_SEQ_CST);

        const char *type_names[] = {"walker", "cyclist", "family"};
        const char *type_name = type_names[type];
        const char *ticket_names[] = {"SINGLE", "T1", "T2", "T3", "DAILY"};
        if (kid_count > 0) {
            log_debug("GENERATOR", "Spawned tourist %d: age=%d, type=%s, vip=%s, kids=%d, ticket=%s (PID %d)",
                     tourist_id, age, type_name, vip ? "yes" : "no", kid_count, ticket_names[ticket], pid);
        } else {
            log_debug("GENERATOR", "Spawned tourist %d: age=%d, type=%s, vip=%s, ticket=%s (PID %d)",
                     tourist_id, age, type_name, vip ? "yes" : "no", ticket_names[ticket], pid);
        }

        // Sleep before next spawn (if delay configured)
        usleep(spawn_delay_us);
    }

    log_info("GENERATOR", "Tourist generator shutting down (spawned %d tourists)", tourist_id);

    // Stop reaper thread first
    stop_reaper_thread();

    // Wait for all children using blocking waitpid (proper synchronization, no polling)
    int current_active = __atomic_load_n(&active_tourists, __ATOMIC_SEQ_CST);
    log_debug("GENERATOR", "Waiting for %d tourists to exit...", current_active);

    pid_t pid;
    int status;
    while (1) {
        pid = waitpid(-1, &status, 0);
        if (pid > 0) {
            log_debug("GENERATOR", "Reaped tourist PID %d", pid);
            continue;
        }
        if (pid == -1 && errno == EINTR) {
            // Signal interrupted waitpid, retry
            continue;
        }
        // ECHILD or other error - no more children
        break;
    }

    log_debug("GENERATOR", "All tourists exited");
}
