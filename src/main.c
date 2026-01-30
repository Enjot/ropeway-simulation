#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>

#include "config.h"
#include "logger.h"
#include "ipc_utils.h"
#include "semaphores.h"
#include "shared_state.h"
#include "tourist.h"

/* Global IPC resources for signal handler cleanup */
static IpcResources g_ipc;
static volatile sig_atomic_t g_running = 1;
static volatile sig_atomic_t g_got_sigchld = 0;

/* Signal handler - async-signal-safe */
static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_running = 0;
    } else if (sig == SIGCHLD) {
        g_got_sigchld = 1;
    }
}

/* Setup signal handlers */
static void setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);
}

/* Reap zombie children */
static void reap_children(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            LOG_DEBUG("Child %d exited with status %d", pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            LOG_DEBUG("Child %d killed by signal %d", pid, WTERMSIG(status));
        }
    }
}

/* Spawn cashier process */
static pid_t spawn_cashier(void) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork cashier");
        return -1;
    }
    if (pid == 0) {
        /* Child process */
        execl("./build/cashier", "cashier", NULL);
        perror("execl cashier");
        _exit(1);
    }
    return pid;
}

/* Spawn tourist process */
static pid_t spawn_tourist(int id, int age, TouristType type, int is_vip) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork tourist");
        return -1;
    }
    if (pid == 0) {
        /* Child process - pass arguments as strings */
        char id_str[16], age_str[16], type_str[16], vip_str[16];
        snprintf(id_str, sizeof(id_str), "%d", id);
        snprintf(age_str, sizeof(age_str), "%d", age);
        snprintf(type_str, sizeof(type_str), "%d", type);
        snprintf(vip_str, sizeof(vip_str), "%d", is_vip);

        execl("./build/tourist", "tourist", id_str, age_str, type_str, vip_str, NULL);
        perror("execl tourist");
        _exit(1);
    }
    return pid;
}

/* Generate random tourist */
static void generate_tourist(int *age, TouristType *type, int *is_vip) {
    /* Age: 8-80 years (must be at least MIN_ADULT_AGE to ride alone) */
    *age = MIN_ADULT_AGE + (rand() % (80 - MIN_ADULT_AGE + 1));

    /* Type: ~50% pedestrian, ~50% cyclist */
    *type = (rand() % 100 < 50) ? TOURIST_PEDESTRIAN : TOURIST_CYCLIST;

    /* VIP: ~1% chance */
    *is_vip = (rand() % 100 < VIP_PERCENT) ? 1 : 0;
}

/* Print final report */
static void print_report(SharedState *state) {
    printf("\n========== DAILY REPORT ==========\n");
    printf("Total rides: %d\n", state->total_rides);
    printf("Total revenue: %d PLN\n", state->total_revenue);
    printf("\nTickets sold:\n");
    printf("  SINGLE: %d\n", state->tickets_sold[TICKET_SINGLE]);
    printf("  TK1:    %d\n", state->tickets_sold[TICKET_TK1]);
    printf("  TK2:    %d\n", state->tickets_sold[TICKET_TK2]);
    printf("  TK3:    %d\n", state->tickets_sold[TICKET_TK3]);
    printf("  DAILY:  %d\n", state->tickets_sold[TICKET_DAILY]);
    printf("\nRides by type:\n");
    printf("  Pedestrian: %d\n", state->pedestrian_rides);
    printf("  Cyclist:    %d\n", state->cyclist_rides);
    printf("\nTourist details:\n");
    for (int i = 0; i < state->tourist_count && i < 50; i++) {
        TouristRecord *t = &state->tourist_records[i];
        printf("  Tourist %d: age=%d, type=%s, ticket=%s, rides=%d, paid=%d PLN\n",
               t->id, t->age, tourist_type_name(t->type),
               ticket_name(t->ticket), t->rides_completed, t->price_paid);
    }
    if (state->tourist_count > 50) {
        printf("  ... and %d more tourists\n", state->tourist_count - 50);
    }
    printf("==================================\n");
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    LOG_INFO("Ropeway simulation starting...");

    /* Seed random number generator */
    srand(time(NULL) ^ getpid());

    /* Setup signal handlers */
    setup_signals();

    /* Create IPC resources */
    LOG_INFO("Creating IPC resources...");
    if (ipc_create(&g_ipc) == -1) {
        LOG_ERROR("Failed to create IPC resources");
        return 1;
    }

    /* Store main PID */
    g_ipc.state->main_pid = getpid();
    g_ipc.state->simulation_start = time(NULL);
    g_ipc.state->ropeway_state = STATE_RUNNING;

    /* Spawn cashier process */
    LOG_INFO("Spawning cashier...");
    pid_t cashier_pid = spawn_cashier();
    if (cashier_pid == -1) {
        LOG_ERROR("Failed to spawn cashier");
        ipc_destroy(&g_ipc);
        return 1;
    }
    g_ipc.state->cashier_pid = cashier_pid;

    /* Wait for cashier to be ready */
    LOG_INFO("Waiting for cashier to be ready...");
    if (sem_wait_safe(g_ipc.sem_id, SEM_CASHIER_READY) == -1) {
        LOG_ERROR("Failed waiting for cashier");
        kill(cashier_pid, SIGTERM);
        ipc_destroy(&g_ipc);
        return 1;
    }
    LOG_INFO("Cashier ready!");

    /* Get number of tourists from config */
    int num_tourists = NUM_TOURISTS;
    LOG_INFO("Spawning %d tourists...", num_tourists);

    /* Spawn tourists */
    int spawned = 0;
    for (int i = 0; i < num_tourists && g_running; i++) {
        /* Reap any finished children */
        if (g_got_sigchld) {
            reap_children();
            g_got_sigchld = 0;
        }

        int age, is_vip;
        TouristType type;
        generate_tourist(&age, &type, &is_vip);

        pid_t tourist_pid = spawn_tourist(i + 1, age, type, is_vip);
        if (tourist_pid == -1) {
            LOG_WARN("Failed to spawn tourist %d", i + 1);
            continue;
        }
        spawned++;

        /* Small delay between spawns to avoid overwhelming the system */
        if (ARRIVAL_DELAY_BASE > 0 || ARRIVAL_DELAY_RAND > 0) {
            int delay = ARRIVAL_DELAY_BASE + (rand() % (ARRIVAL_DELAY_RAND + 1));
            usleep(delay);
        }

        /* Progress logging */
        if ((i + 1) % 100 == 0) {
            LOG_INFO("Spawned %d/%d tourists", i + 1, num_tourists);
        }
    }

    LOG_INFO("Spawned %d tourists, waiting for completion...", spawned);

    /* Wait for all tourists to finish */
    while (g_running) {
        if (g_got_sigchld) {
            reap_children();
            g_got_sigchld = 0;
        }

        /* Check if all tourists have recorded their results */
        if (g_ipc.state->tourist_count >= spawned) {
            LOG_INFO("All %d tourists finished", spawned);
            break;
        }

        usleep(10000);  /* 10ms poll */
    }

    /* Signal shutdown */
    LOG_INFO("Shutting down...");
    g_ipc.state->ropeway_state = STATE_STOPPED;

    /* Wake up cashier so it can check state and exit */
    sem_signal_safe(g_ipc.sem_id, SEM_CASHIER_REQUEST);

    /* Give cashier time to exit cleanly */
    usleep(50000);  /* 50ms */

    /* Stop cashier */
    if (cashier_pid > 0) {
        kill(cashier_pid, SIGTERM);
        waitpid(cashier_pid, NULL, 0);
    }

    /* Reap any remaining children - wait properly */
    while (waitpid(-1, NULL, 0) > 0);

    /* Print report */
    print_report(g_ipc.state);

    /* Cleanup IPC resources */
    LOG_INFO("Cleaning up IPC resources...");
    ipc_destroy(&g_ipc);

    LOG_INFO("Simulation complete");
    return 0;
}
