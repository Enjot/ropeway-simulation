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

static int g_running = 1;

static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_running = 0;
    }
}

// Generate random age (8-80, with some younger kids if adult)
static int generate_age(void) {
    int r = rand() % 100;

    if (r < 5) {
        return 65 + (rand() % 16);  // 5% seniors (65-80)
    } else if (r < 15) {
        return 8 + (rand() % 10);   // 10% young (8-17)
    } else {
        return 18 + (rand() % 47);  // 85% adults (18-64)
    }
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
    sem_wait(res->sem_id, SEM_STATE);
    int paused = res->state->paused;
    sem_post(res->sem_id, SEM_STATE);

    if (paused) {
        log_debug("GENERATOR", "Paused, waiting for resume...");
        sem_wait(res->sem_id, SEM_PAUSE);
        log_debug("GENERATOR", "Resumed");
    }
}

void tourist_generator_main(IPCResources *res, IPCKeys *keys, const char *tourist_exe) {
    (void)keys;

    // Install signal handlers
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
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
        // Check pause
        check_pause(res);

        // Check if closing
        if (res->state->closing) {
            log_info("GENERATOR", "Station closing, stopping tourist generation");
            break;
        }

        // Check if we've reached max concurrent tourists
        // (This is a soft limit - we track approximately)
        if (active_tourists >= res->state->max_concurrent_tourists) {
            usleep(spawn_interval_us);
            // Decrease estimate (tourists exit over time)
            if (active_tourists > 0) active_tourists--;
            continue;
        }

        // Generate tourist attributes
        tourist_id++;
        int age = generate_age();
        TouristType type = generate_type(res->state);
        int vip = is_vip(res->state);

        // Prepare arguments for exec
        char id_str[16];
        char age_str[8];
        char type_str[2];
        char vip_str[2];

        snprintf(id_str, sizeof(id_str), "%d", tourist_id);
        snprintf(age_str, sizeof(age_str), "%d", age);
        snprintf(type_str, sizeof(type_str), "%d", type);
        snprintf(vip_str, sizeof(vip_str), "%d", vip);

        // Fork and exec tourist process
        pid_t pid = fork();

        if (pid == -1) {
            perror("generator: fork");
            usleep(spawn_interval_us);
            continue;
        }

        if (pid == 0) {
            // Child process - exec tourist
            execl(tourist_exe, "tourist", id_str, age_str, type_str, vip_str, NULL);

            // If exec fails
            perror("generator: execl");
            _exit(1);
        }

        // Parent process
        active_tourists++;

        const char *type_name = type == TOURIST_WALKER ? "walker" : "cyclist";
        log_debug("GENERATOR", "Spawned tourist %d: age=%d, type=%s, vip=%s (PID %d)",
                 tourist_id, age, type_name, vip ? "yes" : "no", pid);

        // Sleep before next spawn
        usleep(spawn_interval_us);
    }

    log_info("GENERATOR", "Tourist generator shutting down (spawned %d tourists)", tourist_id);
}
