#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include "config.h"
#include "logger.h"
#include "ipc_utils.h"
#include "semaphores.h"
#include "shared_state.h"
#include "tourist.h"

static IpcResources g_ipc;
static volatile sig_atomic_t g_running = 1;

/* Tourist data */
static int g_tourist_id;
static int g_age;
static TouristType g_type;
static int g_is_vip;
static TicketType g_ticket;
static time_t g_ticket_expires;
static int g_rides_completed = 0;
static int g_price_paid = 0;

/* Signal handler */
static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_running = 0;
    }
}

/* Check if ticket is still valid */
static int ticket_valid(void) {
    if (g_ticket == TICKET_SINGLE) {
        /* Single-use ticket is only valid for one ride */
        return g_rides_completed == 0;
    }
    if (g_ticket_expires == 0) {
        return 0;
    }
    return time(NULL) < g_ticket_expires;
}

/* Buy ticket from cashier */
static int buy_ticket(void) {
    LOG_DEBUG("Tourist %d going to cashier", g_tourist_id);

    /* Get exclusive access to cashier */
    if (sem_wait_safe(g_ipc.sem_id, SEM_CASHIER_QUEUE) == -1) {
        return -1;
    }

    /* Prepare request */
    sem_wait_safe(g_ipc.sem_id, SEM_MUTEX);
    g_ipc.state->cashier_request.tourist_id = g_tourist_id;
    g_ipc.state->cashier_request.age = g_age;
    g_ipc.state->cashier_request.type = g_type;
    g_ipc.state->cashier_request.is_vip = g_is_vip;
    g_ipc.state->cashier_request_ready = 1;
    sem_signal_safe(g_ipc.sem_id, SEM_MUTEX);

    /* Signal cashier that request is ready */
    sem_signal_safe(g_ipc.sem_id, SEM_CASHIER_REQUEST);

    /* Wait for response */
    if (sem_wait_safe(g_ipc.sem_id, SEM_CASHIER_RESPONSE) == -1) {
        sem_signal_safe(g_ipc.sem_id, SEM_CASHIER_QUEUE);
        return -1;
    }

    /* Read response */
    sem_wait_safe(g_ipc.sem_id, SEM_MUTEX);
    g_ticket = g_ipc.state->cashier_response.ticket;
    g_price_paid = g_ipc.state->cashier_response.price;
    g_ticket_expires = g_ipc.state->cashier_response.expires;
    g_ipc.state->cashier_response_ready = 0;
    sem_signal_safe(g_ipc.sem_id, SEM_MUTEX);

    /* Release cashier */
    sem_signal_safe(g_ipc.sem_id, SEM_CASHIER_QUEUE);

    LOG_INFO("Tourist %d bought %s ticket for %d PLN",
             g_tourist_id, ticket_name(g_ticket), g_price_paid);

    return 0;
}

/* Enter the station */
static int enter_station(void) {
    LOG_DEBUG("Tourist %d entering station", g_tourist_id);

    /* Wait for station capacity (VIPs skip this in future milestone) */
    if (sem_wait_safe(g_ipc.sem_id, SEM_STATION_CAPACITY) == -1) {
        return -1;
    }

    /* Update counter */
    sem_wait_safe(g_ipc.sem_id, SEM_MUTEX);
    g_ipc.state->tourists_in_station++;
    sem_signal_safe(g_ipc.sem_id, SEM_MUTEX);

    LOG_DEBUG("Tourist %d entered station (total in station: %d)",
              g_tourist_id, g_ipc.state->tourists_in_station);

    return 0;
}

/* Leave the station */
static void leave_station(void) {
    sem_wait_safe(g_ipc.sem_id, SEM_MUTEX);
    g_ipc.state->tourists_in_station--;
    sem_signal_safe(g_ipc.sem_id, SEM_MUTEX);

    sem_signal_safe(g_ipc.sem_id, SEM_STATION_CAPACITY);

    LOG_DEBUG("Tourist %d left station", g_tourist_id);
}

/* Board a chair */
static int board_chair(void) {
    int slots_needed = tourist_slots(g_type);

    LOG_DEBUG("Tourist %d waiting for chair (%d slots needed)", g_tourist_id, slots_needed);

    /* Wait for available chair slots */
    if (sem_wait_n(g_ipc.sem_id, SEM_CHAIRS_AVAILABLE, slots_needed) == -1) {
        return -1;
    }

    /* Update counters */
    sem_wait_safe(g_ipc.sem_id, SEM_MUTEX);
    g_ipc.state->tourists_in_station--;
    g_ipc.state->tourists_on_chairs++;
    sem_signal_safe(g_ipc.sem_id, SEM_MUTEX);

    /* Release station capacity (we're now on the chair, not in station) */
    sem_signal_safe(g_ipc.sem_id, SEM_STATION_CAPACITY);

    LOG_INFO("Tourist %d boarding chair", g_tourist_id);

    return slots_needed;
}

/* Ride the chair */
static void ride_chair(void) {
    LOG_DEBUG("Tourist %d riding chair", g_tourist_id);
    usleep(RIDE_DURATION_US);
    LOG_DEBUG("Tourist %d arrived at upper station", g_tourist_id);
}

/* Exit via trail */
static int exit_trail(int slots_used) {
    /* Wait for exit capacity */
    if (sem_wait_safe(g_ipc.sem_id, SEM_EXIT_CAPACITY) == -1) {
        return -1;
    }

    /* Update counters */
    sem_wait_safe(g_ipc.sem_id, SEM_MUTEX);
    g_ipc.state->tourists_on_chairs--;
    g_ipc.state->total_rides++;
    if (g_type == TOURIST_CYCLIST) {
        g_ipc.state->cyclist_rides++;
    } else {
        g_ipc.state->pedestrian_rides++;
    }
    sem_signal_safe(g_ipc.sem_id, SEM_MUTEX);

    /* Return chair slots */
    sem_signal_n(g_ipc.sem_id, SEM_CHAIRS_AVAILABLE, slots_used);

    /* Select and use trail */
    int trail_time;
    const char *trail_name;

    if (g_type == TOURIST_CYCLIST) {
        /* Cyclists choose random bike trail */
        int trail = rand() % 3;
        switch (trail) {
            case 0:
                trail_time = TRAIL_EASY_US;
                trail_name = "easy bike";
                break;
            case 1:
                trail_time = TRAIL_MEDIUM_US;
                trail_name = "medium bike";
                break;
            default:
                trail_time = TRAIL_HARD_US;
                trail_name = "hard bike";
                break;
        }
    } else {
        /* Pedestrians use walking trail */
        trail_time = TRAIL_EASY_US / 2;  /* Walking is faster */
        trail_name = "walking";
    }

    LOG_INFO("Tourist %d exiting via %s trail", g_tourist_id, trail_name);
    usleep(trail_time);

    /* Release exit capacity */
    sem_signal_safe(g_ipc.sem_id, SEM_EXIT_CAPACITY);

    g_rides_completed++;
    LOG_INFO("Tourist %d completed ride #%d", g_tourist_id, g_rides_completed);

    return 0;
}

/* Record tourist in shared state for report */
static void record_tourist(void) {
    sem_wait_safe(g_ipc.sem_id, SEM_MUTEX);

    int idx = g_ipc.state->tourist_count;
    if (idx < MAX_TOURISTS) {
        TouristRecord *rec = &g_ipc.state->tourist_records[idx];
        rec->id = g_tourist_id;
        rec->age = g_age;
        rec->type = g_type;
        rec->ticket = g_ticket;
        rec->is_vip = g_is_vip;
        rec->rides_completed = g_rides_completed;
        rec->price_paid = g_price_paid;
        g_ipc.state->tourist_count++;
    }

    sem_signal_safe(g_ipc.sem_id, SEM_MUTEX);
}

/* Decide if tourist wants to ride */
static int wants_to_ride(void) {
    if (ALL_TOURISTS_RIDE) {
        return 1;
    }
    /* ~90% want to ride */
    return (rand() % 100) < 90;
}

int main(int argc, char *argv[]) {
    /* Parse arguments: id, age, type, is_vip */
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <id> <age> <type> <is_vip>\n", argv[0]);
        return 1;
    }

    g_tourist_id = atoi(argv[1]);
    g_age = atoi(argv[2]);
    g_type = (TouristType)atoi(argv[3]);
    g_is_vip = atoi(argv[4]);

    LOG_DEBUG("Tourist %d started (age=%d, %s%s)",
              g_tourist_id, g_age, tourist_type_name(g_type),
              g_is_vip ? ", VIP" : "");

    /* Setup signal handler */
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    /* Seed random */
    srand(time(NULL) ^ getpid() ^ g_tourist_id);

    /* Attach to IPC resources */
    if (ipc_attach(&g_ipc) == -1) {
        LOG_ERROR("Tourist %d failed to attach to IPC", g_tourist_id);
        return 1;
    }

    /* Check if ropeway is running */
    if (g_ipc.state->ropeway_state != STATE_RUNNING) {
        LOG_INFO("Tourist %d: Ropeway not running, leaving", g_tourist_id);
        ipc_detach(&g_ipc);
        return 0;
    }

    /* Buy ticket */
    if (buy_ticket() == -1) {
        LOG_ERROR("Tourist %d failed to buy ticket", g_tourist_id);
        ipc_detach(&g_ipc);
        return 1;
    }

    /* Main loop - ride until ticket expires */
    while (g_running && ticket_valid() && g_ipc.state->ropeway_state == STATE_RUNNING) {
        /* Check if tourist wants to ride */
        if (!wants_to_ride()) {
            LOG_INFO("Tourist %d doesn't want to ride, leaving", g_tourist_id);
            break;
        }

        /* Enter station */
        if (enter_station() == -1) {
            break;
        }

        /* Check ropeway state after entering */
        if (g_ipc.state->ropeway_state != STATE_RUNNING) {
            leave_station();
            break;
        }

        /* Board chair */
        int slots_used = board_chair();
        if (slots_used == -1) {
            leave_station();
            break;
        }

        /* Ride */
        ride_chair();

        /* Exit via trail */
        if (exit_trail(slots_used) == -1) {
            break;
        }

        /* Check if can ride again */
        if (!ticket_valid()) {
            LOG_INFO("Tourist %d: ticket expired after %d rides", g_tourist_id, g_rides_completed);
            break;
        }
    }

    /* Record final stats */
    record_tourist();

    /* Cleanup */
    ipc_detach(&g_ipc);

    LOG_INFO("Tourist %d leaving (completed %d rides)", g_tourist_id, g_rides_completed);

    return 0;
}
