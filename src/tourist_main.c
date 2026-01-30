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
#include <sys/msg.h>
#include <time.h>

// Tourist data structure
typedef struct {
    int id;
    int age;
    TouristType type;
    int is_vip;
    TicketType ticket_type;
    int ticket_valid_until;  // Sim minutes
    int rides_completed;
    int slots_needed;        // 1 for walker, 2 for cyclist
} TouristData;

static int g_running = 1;

static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_running = 0;
    }
}

// Parse command line arguments
static int parse_args(int argc, char *argv[], TouristData *data) {
    if (argc != 5) {
        fprintf(stderr, "Usage: tourist <id> <age> <type> <vip>\n");
        return -1;
    }

    data->id = atoi(argv[1]);
    data->age = atoi(argv[2]);
    data->type = atoi(argv[3]);
    data->is_vip = atoi(argv[4]);
    data->rides_completed = 0;
    data->slots_needed = (data->type == TOURIST_CYCLIST) ? 2 : 1;
    data->ticket_type = -1;  // Not yet purchased
    data->ticket_valid_until = 0;

    return 0;
}

// Issue #11 fix: Use consolidated ipc_check_pause() instead of duplicated code
// See ipc.c for implementation - wrapper for consistency
static void check_pause(IPCResources *res) {
    ipc_check_pause(res);
}

// Pause-aware sleep (handles SIGTSTP during sleep)
static int pauseable_sleep(IPCResources *res, double real_seconds) {
    time_t start = time(NULL);
    double remaining = real_seconds;

    while (remaining > 0 && g_running && res->state->running) {
        struct timespec ts;
        ts.tv_sec = (time_t)remaining;
        ts.tv_nsec = (long)((remaining - ts.tv_sec) * 1e9);

        int ret = nanosleep(&ts, &ts);

        if (ret == -1 && errno == EINTR) {
            // Check if paused
            check_pause(res);
            // Recalculate remaining
            remaining = real_seconds - (double)(time(NULL) - start);
        } else {
            break;
        }
    }

    return g_running && res->state->running ? 0 : -1;
}

// Buy ticket at cashier
static int buy_ticket(IPCResources *res, TouristData *data) {
    CashierMsg request;
    request.mtype = 1;  // Any positive value
    request.tourist_id = data->id;
    request.tourist_type = data->type;
    request.age = data->age;
    request.is_vip = data->is_vip;

    // Send request
    if (msgsnd(res->mq_cashier_id, &request, sizeof(request) - sizeof(long), 0) == -1) {
        // Issue #6 fix: Check for EIDRM
        if (errno == EIDRM) return -1;
        perror("tourist: msgsnd cashier request");
        return -1;
    }

    // Wait for response (mtype = tourist_id)
    CashierMsg response;
    while (1) {
        ssize_t ret = msgrcv(res->mq_cashier_id, &response,
                             sizeof(response) - sizeof(long), data->id, 0);
        if (ret == -1) {
            if (errno == EINTR) continue;
            // Issue #6 fix: Check for EIDRM
            if (errno == EIDRM) return -1;
            perror("tourist: msgrcv cashier response");
            return -1;
        }
        break;
    }

    if (response.ticket_type == -1) {
        // Rejected (station closing)
        return -1;
    }

    data->ticket_type = response.ticket_type;
    data->ticket_valid_until = response.ticket_valid_until;

    return 0;
}

// Check if ticket is still valid
static int is_ticket_valid(IPCResources *res, TouristData *data) {
    // Single-use tickets are valid until used once
    if (data->ticket_type == TICKET_SINGLE) {
        return data->rides_completed == 0;
    }

    // Time-based and daily tickets: check expiration
    int current_minutes = time_get_sim_minutes(res->state);
    return current_minutes < data->ticket_valid_until;
}

// Check if station is closing
static int is_station_closing(IPCResources *res) {
    sem_wait(res->sem_id, SEM_STATE);
    int closing = res->state->closing;
    sem_post(res->sem_id, SEM_STATE);
    return closing;
}

// Wait in station for chair
static int board_chair(IPCResources *res, TouristData *data) {
    // Get a chair slot (blocks if all in use)
    if (sem_wait(res->sem_id, SEM_CHAIRS) == -1) {
        return -1;
    }

    check_pause(res);

    // Issue #2, #4 fix: Check emergency stop with semaphore protection and blocking
    sem_wait(res->sem_id, SEM_STATE);
    int emergency = res->state->emergency_stop;
    sem_post(res->sem_id, SEM_STATE);

    if (emergency) {
        // Issue #4 fix: Block on semaphore instead of polling with usleep
        ipc_wait_emergency_clear(res);
    }

    // Send "ready to board" message to lower worker
    PlatformMsg msg;
    msg.mtype = data->is_vip ? 1 : 2;  // VIPs get priority
    msg.tourist_id = data->id;
    msg.tourist_type = data->type;
    msg.slots_needed = data->slots_needed;

    if (msgsnd(res->mq_platform_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
        if (errno == EINTR) return -1;
        // Issue #6 fix: Check for EIDRM
        if (errno == EIDRM) {
            sem_post(res->sem_id, SEM_CHAIRS);
            return -1;
        }
        perror("tourist: msgsnd platform");
        sem_post(res->sem_id, SEM_CHAIRS);  // Release chair slot
        return -1;
    }

    // Wait for boarding confirmation (mtype = tourist_id)
    PlatformMsg response;
    while (1) {
        ssize_t ret = msgrcv(res->mq_boarding_id, &response,
                             sizeof(response) - sizeof(long), data->id, 0);
        if (ret == -1) {
            if (errno == EINTR) {
                check_pause(res);
                continue;
            }
            // Issue #6 fix: Check for EIDRM
            if (errno == EIDRM) {
                sem_post(res->sem_id, SEM_CHAIRS);
                return -1;
            }
            perror("tourist: msgrcv boarding");
            sem_post(res->sem_id, SEM_CHAIRS);
            return -1;
        }
        break;
    }

    return 0;
}

// Ride the chairlift
static int ride_chairlift(IPCResources *res, TouristData *data) {
    (void)data;

    // Calculate ride time
    double ride_seconds = time_sim_to_real_seconds(res->state, res->state->chair_travel_time_sim);

    log_debug("TOURIST", "Tourist %d riding chairlift (%.1f real seconds)",
              data->id, ride_seconds);

    return pauseable_sleep(res, ride_seconds);
}

// Arrive at upper platform
static int arrive_upper(IPCResources *res, TouristData *data) {
    // Release chair slot
    sem_post(res->sem_id, SEM_CHAIRS);

    // Wait for exit gate
    if (sem_wait(res->sem_id, SEM_EXIT_GATES) == -1) {
        return -1;
    }

    check_pause(res);

    // Notify upper worker of arrival
    ArrivalMsg msg;
    msg.mtype = 1;
    msg.tourist_id = data->id;

    if (msgsnd(res->mq_arrivals_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
        // Issue #6 fix: Check for EIDRM (queue removed during shutdown)
        if (errno != EINTR && errno != EIDRM) {
            perror("tourist: msgsnd arrivals");
        }
    }

    // Release exit gate
    sem_post(res->sem_id, SEM_EXIT_GATES);

    return 0;
}

// Walk/bike the trail back down
static int descend_trail(IPCResources *res, TouristData *data) {
    int trail_time_sim;

    if (data->type == TOURIST_WALKER) {
        trail_time_sim = res->state->trail_walk_time;
    } else {
        // Cyclists randomly pick a trail
        int r = rand() % 3;
        switch (r) {
            case 0:
                trail_time_sim = res->state->trail_bike_fast_time;
                break;
            case 1:
                trail_time_sim = res->state->trail_bike_medium_time;
                break;
            default:
                trail_time_sim = res->state->trail_bike_slow_time;
                break;
        }
    }

    double trail_seconds = time_sim_to_real_seconds(res->state, trail_time_sim);

    log_debug("TOURIST", "Tourist %d descending trail (%.1f real seconds)",
              data->id, trail_seconds);

    return pauseable_sleep(res, trail_seconds);
}

// Update statistics
static void update_stats(IPCResources *res, TouristData *data) {
    sem_wait(res->sem_id, SEM_STATS);
    res->state->total_rides++;
    if (data->ticket_type >= 0 && data->ticket_type < TICKET_COUNT) {
        res->state->rides_by_ticket[data->ticket_type]++;
    }
    sem_post(res->sem_id, SEM_STATS);
}

int main(int argc, char *argv[]) {
    TouristData data;

    if (parse_args(argc, argv, &data) == -1) {
        return 1;
    }

    // Install signal handlers
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    // Seed random number generator
    srand(time(NULL) ^ getpid());

    // Generate IPC keys (using same path as main)
    IPCKeys keys;
    // We need to use the same path as main - assume it's in current directory
    if (ipc_generate_keys(&keys, "./ropeway_simulation") == -1) {
        fprintf(stderr, "tourist %d: Failed to generate IPC keys\n", data.id);
        return 1;
    }

    // Attach to IPC resources
    IPCResources res;
    if (ipc_attach(&res, &keys) == -1) {
        fprintf(stderr, "tourist %d: Failed to attach to IPC\n", data.id);
        return 1;
    }

    // Initialize logger
    logger_init(res.state);

    const char *type_name = data.type == TOURIST_WALKER ? "walker" : "cyclist";
    log_info("TOURIST", "Tourist %d arrived (age %d, %s%s)",
             data.id, data.age, type_name, data.is_vip ? ", VIP" : "");

    // Buy ticket at cashier
    if (buy_ticket(&res, &data) == -1) {
        log_info("TOURIST", "Tourist %d leaving (no ticket)", data.id);
        ipc_detach(&res);
        return 0;
    }

    const char *ticket_names[] = {"SINGLE", "TIME_T1", "TIME_T2", "TIME_T3", "DAILY"};
    log_debug("TOURIST", "Tourist %d got %s ticket",
              data.id, ticket_names[data.ticket_type]);

    // Main ride loop
    while (g_running && res.state->running) {
        check_pause(&res);

        // Check exit conditions
        if (!is_ticket_valid(&res, &data)) {
            log_info("TOURIST", "Tourist %d leaving (ticket expired)", data.id);
            break;
        }

        if (is_station_closing(&res)) {
            log_info("TOURIST", "Tourist %d leaving (station closing)", data.id);
            break;
        }

        // ~10% decide not to ride again
        if (data.rides_completed > 0 && (rand() % 10) == 0) {
            log_info("TOURIST", "Tourist %d leaving (decided to go home)", data.id);
            break;
        }

        // Enter through entry gate
        if (sem_wait(res.sem_id, SEM_ENTRY_GATES) == -1) {
            break;
        }

        check_pause(&res);

        log_debug("TOURIST", "Tourist %d entered through gate", data.id);

        // Enter lower station (wait if full)
        if (sem_wait(res.sem_id, SEM_LOWER_STATION) == -1) {
            sem_post(res.sem_id, SEM_ENTRY_GATES);
            break;
        }

        // Update station count for logging
        sem_wait(res.sem_id, SEM_STATE);
        res.state->lower_station_count++;
        int count = res.state->lower_station_count;
        sem_post(res.sem_id, SEM_STATE);

        // Release entry gate now that we're in station
        sem_post(res.sem_id, SEM_ENTRY_GATES);

        log_debug("TOURIST", "Tourist %d in lower station (count: %d/%d)",
                  data.id, count, res.state->station_capacity);

        check_pause(&res);

        // Wait for platform gate (3 gates on lower platform)
        if (sem_wait(res.sem_id, SEM_PLATFORM_GATES) == -1) {
            sem_wait(res.sem_id, SEM_STATE);
            res.state->lower_station_count--;
            sem_post(res.sem_id, SEM_STATE);
            sem_post(res.sem_id, SEM_LOWER_STATION);
            break;
        }

        log_debug("TOURIST", "Tourist %d passed through platform gate", data.id);
        check_pause(&res);

        // Board chair
        if (board_chair(&res, &data) == -1) {
            // Release platform gate and station slot
            sem_post(res.sem_id, SEM_PLATFORM_GATES);
            sem_wait(res.sem_id, SEM_STATE);
            res.state->lower_station_count--;
            sem_post(res.sem_id, SEM_STATE);
            sem_post(res.sem_id, SEM_LOWER_STATION);
            break;
        }

        // Release platform gate (now boarding chair)
        sem_post(res.sem_id, SEM_PLATFORM_GATES);

        // Release station slot (now on chair)
        sem_wait(res.sem_id, SEM_STATE);
        res.state->lower_station_count--;
        sem_post(res.sem_id, SEM_STATE);
        sem_post(res.sem_id, SEM_LOWER_STATION);

        log_info("TOURIST", "Tourist %d boarded chairlift", data.id);

        // Ride chairlift
        if (ride_chairlift(&res, &data) == -1) {
            // Still need to release chair and arrive
            sem_post(res.sem_id, SEM_CHAIRS);
            break;
        }

        // Arrive at upper platform
        if (arrive_upper(&res, &data) == -1) {
            break;
        }

        // Descend trail
        if (descend_trail(&res, &data) == -1) {
            break;
        }

        data.rides_completed++;
        update_stats(&res, &data);

        log_info("TOURIST", "Tourist %d completed ride #%d",
                 data.id, data.rides_completed);

        // Single ticket: one ride only
        if (data.ticket_type == TICKET_SINGLE) {
            log_info("TOURIST", "Tourist %d leaving (single ticket used)", data.id);
            break;
        }
    }

    log_info("TOURIST", "Tourist %d exiting (total rides: %d)",
             data.id, data.rides_completed);

    ipc_detach(&res);
    return 0;
}
