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
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>

// Tourist data structure
typedef struct TouristData {
    int id;
    int age;
    TouristType type;
    int is_vip;
    TicketType ticket_type;
    int ticket_valid_until;  // Sim minutes
    int rides_completed;
    int station_slots;       // For lower station: 1 + kid_count (bike doesn't count)
    int chair_slots;         // For chair: walker=1, cyclist=2, plus kid_count
    int kid_count;           // Number of kids (0-2)
} TouristData;

// Family state for kid/bike threads (simplified - just data, no sync primitives)
typedef struct FamilyState {
    int parent_id;
    int kid_count;
    int has_bike;
    IPCResources *res;
} FamilyState;

// Per-thread data for kid threads
typedef struct KidThreadData {
    int kid_index;           // 0 or 1 for kids
    FamilyState *family;
} KidThreadData;

// Per-thread data for bike thread
typedef struct BikeThreadData {
    int tourist_id;
    FamilyState *family;
} BikeThreadData;

static int g_running = 1;

static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_running = 0;
    }
}

/**
 * Parse command line arguments for tourist process.
 * Format: tourist <id> <age> <type> <vip> <kid_count> <ticket_type>
 */
static int parse_args(int argc, char *argv[], TouristData *data) {
    if (argc != 7) {
        fprintf(stderr, "Usage: tourist <id> <age> <type> <vip> <kid_count> <ticket_type>\n");
        return -1;
    }

    data->id = atoi(argv[1]);
    data->age = atoi(argv[2]);
    data->type = atoi(argv[3]);
    data->is_vip = atoi(argv[4]);
    data->kid_count = atoi(argv[5]);
    data->ticket_type = atoi(argv[6]);

    data->rides_completed = 0;
    data->ticket_valid_until = 0;

    // Validate constraints
    if (data->kid_count > 0) {
        if (data->type != TOURIST_WALKER) {
            fprintf(stderr, "Error: Only walkers can have kids\n");
            return -1;
        }
        if (data->age < 26) {
            fprintf(stderr, "Error: Must be 26+ to be a guardian\n");
            return -1;
        }
        if (data->kid_count > MAX_KIDS_PER_ADULT) {
            fprintf(stderr, "Error: Maximum %d kids per adult\n", MAX_KIDS_PER_ADULT);
            return -1;
        }
    }

    // Station capacity: person + kids (bike doesn't take waiting room space)
    data->station_slots = 1 + data->kid_count;

    // Chair capacity: walker=1, cyclist=2 (for bike), plus kids
    int parent_chair = (data->type == TOURIST_CYCLIST) ? 2 : 1;
    data->chair_slots = parent_chair + data->kid_count;

    return 0;
}

// Issue #11 fix: Use consolidated ipc_check_pause() instead of duplicated code
// See ipc.c for implementation - wrapper for consistency
static void check_pause(IPCResources *res) {
    ipc_check_pause(res);
}

/**
 * Kid thread function.
 * Kids just log and exit immediately - parent handles all actions.
 * Thread resources are cleaned up via pthread_join() at parent cleanup.
 */
static void *kid_thread_func(void *arg) {
    KidThreadData *td = (KidThreadData *)arg;
    FamilyState *family = td->family;
    int kid_idx = td->kid_index;

    log_info("KID", "Tourist %d's kid #%d started", family->parent_id, kid_idx + 1);
    // Thread exits immediately - no zombie, just waits for join
    log_info("KID", "Tourist %d's kid #%d exiting", family->parent_id, kid_idx + 1);

    return NULL;
}

/**
 * Bike thread function (for cyclists).
 * Bikes just log and exit immediately - parent handles all actions.
 * Thread resources are cleaned up via pthread_join() at parent cleanup.
 */
static void *bike_thread_func(void *arg) {
    BikeThreadData *td = (BikeThreadData *)arg;

    log_info("BIKE", "Tourist %d's bike ready", td->tourist_id);
    // Thread exits immediately
    log_info("BIKE", "Tourist %d's bike stored", td->tourist_id);

    return NULL;
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

/**
 * Buy ticket at cashier.
 * For families, parent buys tickets for all kids (same type).
 */
static int buy_ticket(IPCResources *res, TouristData *data) {
    CashierMsg request;
    request.mtype = MSG_CASHIER_REQUEST;
    request.tourist_id = data->id;
    request.tourist_type = data->type;
    request.age = data->age;
    request.is_vip = data->is_vip;
    request.kid_count = data->kid_count;
    request.ticket_type = data->ticket_type;

    // Send request
    if (msgsnd(res->mq_cashier_id, &request, sizeof(request) - sizeof(long), 0) == -1) {
        // Issue #6 fix: Check for EIDRM
        if (errno == EIDRM) return -1;
        perror("tourist: msgsnd cashier request");
        return -1;
    }

    // Wait for response (mtype = MSG_CASHIER_RESPONSE_BASE + tourist_id)
    CashierMsg response;
    while (1) {
        ssize_t ret = msgrcv(res->mq_cashier_id, &response,
                             sizeof(response) - sizeof(long),
                             MSG_CASHIER_RESPONSE_BASE + data->id, 0);
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
    if (sem_wait_pauseable(res, SEM_STATE, 1) == -1) {
        return 1;  // Assume closing on failure (shutdown)
    }
    int closing = res->state->closing;
    sem_post(res->sem_id, SEM_STATE, 1);
    return closing;
}

// Wait in station for chair
// departure_time_out: receives the chair's departure timestamp for synchronized arrival
// chair_id_out, tourists_on_chair_out: for upper worker tracking (to release SEM_CHAIRS atomically)
static int board_chair(IPCResources *res, TouristData *data, time_t *departure_time_out,
                       int *chair_id_out, int *tourists_on_chair_out) {
    // Note: SEM_CHAIRS is now acquired by lower_worker when chair departs,
    // and released by upper_worker when all tourists from that chair arrive.

    check_pause(res);

    // Issue #2, #4 fix: Check emergency stop with semaphore protection and blocking
    if (sem_wait_pauseable(res, SEM_STATE, 1) == -1) {
        return -1;
    }
    int emergency = res->state->emergency_stop;
    sem_post(res->sem_id, SEM_STATE, 1);

    if (emergency) {
        // Issue #4 fix: Block on semaphore instead of polling with usleep
        ipc_wait_emergency_clear(res);
    }

    // Send "ready to board" message to lower worker
    PlatformMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.mtype = 2;  // No VIP priority at boarding (VIPs skip entry gates instead)
    msg.tourist_id = data->id;
    msg.tourist_type = data->type;
    msg.slots_needed = data->chair_slots;  // Chair slots include bike for cyclists
    msg.kid_count = data->kid_count;

    if (msgsnd(res->mq_platform_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
        if (errno == EINTR) return -1;
        // Issue #6 fix: Check for EIDRM
        if (errno == EIDRM) {
            return -1;
        }
        perror("tourist: msgsnd platform");
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
                return -1;
            }
            perror("tourist: msgrcv boarding");
            return -1;
        }
        break;
    }

    // Return the departure time and chair info for synchronized arrival
    if (departure_time_out) *departure_time_out = response.departure_time;
    if (chair_id_out) *chair_id_out = response.chair_id;
    if (tourists_on_chair_out) *tourists_on_chair_out = response.tourists_on_chair;

    return 0;
}

// Ride the chairlift (synchronized with other passengers via departure_time)
static int ride_chairlift(IPCResources *res, TouristData *data, time_t departure_time) {
    // Calculate expected arrival time
    double travel_seconds = time_sim_to_real_seconds(res->state, res->state->chair_travel_time_sim);
    time_t arrival_time = departure_time + (time_t)travel_seconds;

    // Calculate how long we still need to wait
    time_t now = time(NULL);
    double remaining = (double)(arrival_time - now);

    log_info("TOURIST", "%d riding chairlift (%.1f real seconds)",
             data->id, travel_seconds);

    // Only sleep if we haven't already reached arrival time
    if (remaining > 0) {
        return pauseable_sleep(res, remaining);
    }

    return 0;
}

// Arrive at upper platform
// chair_id and tourists_on_chair are passed to upper_worker for atomic SEM_CHAIRS release
static int arrive_upper(IPCResources *res, TouristData *data,
                        int chair_id, int tourists_on_chair) {
    // Note: SEM_CHAIRS is released by upper_worker when all tourists from chair arrive

    // Wait for exit gate
    if (sem_wait_pauseable(res, SEM_EXIT_GATES, 1) == -1) {
        return -1;
    }

    check_pause(res);

    // Notify upper worker of arrival (with chair info for tracking)
    ArrivalMsg msg;
    msg.mtype = 1;
    msg.tourist_id = data->id;
    msg.kid_count = data->kid_count;  // For family logging at upper platform
    msg.chair_id = chair_id;
    msg.tourists_on_chair = tourists_on_chair;

    if (msgsnd(res->mq_arrivals_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
        // Issue #6 fix: Check for EIDRM (queue removed during shutdown)
        if (errno != EINTR && errno != EIDRM) {
            perror("tourist: msgsnd arrivals");
        }
    }

    // Release exit gate
    sem_post(res->sem_id, SEM_EXIT_GATES, 1);

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

    log_info("TOURIST", "%d descending trail (%.1f real seconds)",
             data->id, trail_seconds);

    return pauseable_sleep(res, trail_seconds);
}

/**
 * Record tourist entry in shared memory for final report.
 * Called once when tourist first enters the system with valid ticket.
 */
static void record_tourist_entry(IPCResources *res, TouristData *data) {
    if (sem_wait_pauseable(res, SEM_STATS, 1) == -1) {
        return;  // Shutdown in progress
    }

    int idx = data->id - 1;
    if (idx >= 0 && idx < res->state->max_tracked_tourists) {
        TouristEntry *entry = &res->state->tourist_entries[idx];
        entry->active = 1;
        entry->tourist_id = data->id;
        entry->ticket_type = data->ticket_type;
        entry->entry_time_sim = time_get_sim_minutes(res->state);
        entry->total_rides = 0;
        entry->is_vip = data->is_vip;
        entry->tourist_type = data->type;
        entry->kid_count = data->kid_count;

        if (idx >= res->state->tourist_entry_count) {
            res->state->tourist_entry_count = idx + 1;
        }
    }

    sem_post(res->sem_id, SEM_STATS, 1);
}

/**
 * Update statistics for completed ride.
 * Counts rides for parent and all kids in the family.
 */
static void update_stats(IPCResources *res, TouristData *data) {
    if (sem_wait_pauseable(res, SEM_STATS, 1) == -1) {
        return;  // Shutdown in progress
    }

    // Count parent ride
    res->state->total_rides++;
    if (data->ticket_type >= 0 && data->ticket_type < TICKET_COUNT) {
        res->state->rides_by_ticket[data->ticket_type]++;
    }

    // Update per-tourist ride count
    int idx = data->id - 1;
    if (idx >= 0 && idx < res->state->max_tracked_tourists) {
        res->state->tourist_entries[idx].total_rides++;
    }

    // Count kid rides (same ticket type as parent)
    for (int i = 0; i < data->kid_count; i++) {
        res->state->total_rides++;
        if (data->ticket_type >= 0 && data->ticket_type < TICKET_COUNT) {
            res->state->rides_by_ticket[data->ticket_type]++;
        }
    }

    sem_post(res->sem_id, SEM_STATS, 1);
}

int main(int argc, char *argv[]) {
    TouristData data;
    FamilyState family;
    pthread_t kid_threads[MAX_KIDS_PER_ADULT];
    KidThreadData kid_data[MAX_KIDS_PER_ADULT];
    pthread_t bike_thread;
    BikeThreadData bike_data;
    int bike_thread_created = 0;

    memset(&family, 0, sizeof(family));
    memset(&bike_data, 0, sizeof(bike_data));

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

    // Generate IPC keys (using current directory - same for all processes)
    IPCKeys keys;
    if (ipc_generate_keys(&keys, ".") == -1) {
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
    logger_init(res.state, LOG_TOURIST);
    logger_set_debug_enabled(res.state->debug_logs_enabled);

    // Initialize family state (simple data only - no sync primitives)
    family.parent_id = data.id;
    family.kid_count = data.kid_count;
    family.has_bike = (data.type == TOURIST_CYCLIST);
    family.res = &res;

    // Spawn bike thread if cyclist (thread exits immediately after logging)
    if (data.type == TOURIST_CYCLIST) {
        bike_data.tourist_id = data.id;
        bike_data.family = &family;
        if (pthread_create(&bike_thread, NULL, bike_thread_func, &bike_data) != 0) {
            perror("tourist: pthread_create bike");
            ipc_detach(&res);
            return 1;
        }
        bike_thread_created = 1;
    }

    // Spawn kid threads (threads exit immediately after logging)
    if (data.kid_count > 0) {
        for (int i = 0; i < data.kid_count; i++) {
            kid_data[i].kid_index = i;
            kid_data[i].family = &family;

            if (pthread_create(&kid_threads[i], NULL, kid_thread_func, &kid_data[i]) != 0) {
                perror("tourist: pthread_create kid");
                // Join already-created threads
                for (int j = 0; j < i; j++) {
                    pthread_join(kid_threads[j], NULL);
                }
                if (bike_thread_created) {
                    pthread_join(bike_thread, NULL);
                }
                ipc_detach(&res);
                return 1;
            }
        }
    }

    const char *type_name = data.type == TOURIST_WALKER ? "walker" : "cyclist";
    if (data.kid_count > 0) {
        log_info("TOURIST", "%d arrived (age %d, %s%s) with %d kid(s)",
                 data.id, data.age, type_name, data.is_vip ? ", VIP" : "", data.kid_count);
    } else {
        log_info("TOURIST", "%d arrived (age %d, %s%s)",
                 data.id, data.age, type_name, data.is_vip ? ", VIP" : "");
    }

    // Buy ticket at cashier (parent buys for whole family)
    if (buy_ticket(&res, &data) == -1) {
        log_info("TOURIST", "%d leaving (no ticket)", data.id);
        goto cleanup_family;
    }

    const char *ticket_names[] = {"SINGLE", "TIME_T1", "TIME_T2", "TIME_T3", "DAILY"};
    if (data.kid_count > 0) {
        log_info("TOURIST", "%d got %s family ticket for %d",
                 data.id, ticket_names[data.ticket_type], 1 + data.kid_count);
    } else {
        log_info("TOURIST", "%d got %s ticket",
                 data.id, ticket_names[data.ticket_type]);
    }

    // Record tourist entry for final report
    record_tourist_entry(&res, &data);

    // Main ride loop
    while (g_running && res.state->running) {
        check_pause(&res);

        // Check exit conditions
        if (!is_ticket_valid(&res, &data)) {
            log_info("TOURIST", "%d leaving (ticket expired)", data.id);
            break;
        }

        if (is_station_closing(&res)) {
            log_info("TOURIST", "%d leaving (station closing)", data.id);
            break;
        }

        // Enter through entry gate (VIPs skip the queue)
        if (!data.is_vip) {
            if (sem_wait_pauseable(&res, SEM_ENTRY_GATES, 1) == -1) {
                break;
            }
        }

        check_pause(&res);

        log_info("TOURIST", "%d entered through gate", data.id);

        // Enter lower station (wait if full)
        // Station slots = person + kids (bike doesn't count in waiting room)
        if (sem_wait_pauseable(&res, SEM_LOWER_STATION, data.station_slots) == -1) {
            if (!data.is_vip) sem_post(res.sem_id, SEM_ENTRY_GATES, 1);
            break;
        }

        // Update station count for logging
        if (sem_wait_pauseable(&res, SEM_STATE, 1) == -1) {
            if (!data.is_vip) sem_post(res.sem_id, SEM_ENTRY_GATES, 1);
            sem_post(res.sem_id, SEM_LOWER_STATION, data.station_slots);
            break;
        }
        res.state->lower_station_count += data.station_slots;
        int count = res.state->lower_station_count;
        sem_post(res.sem_id, SEM_STATE, 1);

        // Release entry gate now that we're in station (only if we acquired one)
        if (!data.is_vip) {
            sem_post(res.sem_id, SEM_ENTRY_GATES, 1);
        }

        if (data.kid_count > 0) {
            log_info("TOURIST", "%d + %d kids in lower station (count: %d/%d)",
                     data.id, data.kid_count, count, res.state->station_capacity);
        } else {
            log_info("TOURIST", "%d in lower station (count: %d/%d)",
                     data.id, count, res.state->station_capacity);
        }

        check_pause(&res);

        // ~10% chance to leave: before first ride (too scared) or after first ride (was too scary)
        if ((data.rides_completed == 0 || data.rides_completed == 1) && (rand() % 10) == 0) {
            const char *reason = (data.rides_completed == 0) ? "too scared to ride" : "that was too scary";
            if (data.kid_count > 0) {
                log_info("TOURIST", "%d + %d kids leaving lower station (%s)",
                         data.id, data.kid_count, reason);
            } else {
                log_info("TOURIST", "%d leaving lower station (%s)", data.id, reason);
            }
            // Release lower station slots (skip state update if shutdown)
            if (sem_wait_pauseable(&res, SEM_STATE, 1) == 0) {
                res.state->lower_station_count -= data.station_slots;
                sem_post(res.sem_id, SEM_STATE, 1);
            }
            sem_post(res.sem_id, SEM_LOWER_STATION, data.station_slots);
            break;
        }

        // Wait for platform gate (3 gates on lower platform)
        if (sem_wait_pauseable(&res, SEM_PLATFORM_GATES, 1) == -1) {
            // Cleanup on failure - skip state update if shutdown
            if (sem_wait_pauseable(&res, SEM_STATE, 1) == 0) {
                res.state->lower_station_count -= data.station_slots;
                sem_post(res.sem_id, SEM_STATE, 1);
            }
            sem_post(res.sem_id, SEM_LOWER_STATION, data.station_slots);
            break;
        }

        log_info("TOURIST", "%d passed through platform gate", data.id);

        // Release station slots now that we're past the platform gate
        // (we're committed to boarding, so free up station capacity for others)
        if (sem_wait_pauseable(&res, SEM_STATE, 1) == 0) {
            res.state->lower_station_count -= data.station_slots;
            sem_post(res.sem_id, SEM_STATE, 1);
        }
        sem_post(res.sem_id, SEM_LOWER_STATION, data.station_slots);

        check_pause(&res);

        // Board chair (family boards together)
        time_t departure_time = 0;
        int chair_id = 0;
        int tourists_on_chair = 0;
        if (board_chair(&res, &data, &departure_time, &chair_id, &tourists_on_chair) == -1) {
            // Release platform gate only (station slots already released after platform gate)
            sem_post(res.sem_id, SEM_PLATFORM_GATES, 1);
            break;
        }

        // Release platform gate (now boarding chair)
        sem_post(res.sem_id, SEM_PLATFORM_GATES, 1);

        if (data.kid_count > 0) {
            log_info("TOURIST", "%d + %d kids boarded chairlift", data.id, data.kid_count);
        } else {
            log_info("TOURIST", "%d boarded chairlift", data.id);
        }

        // Ride chairlift (synchronized with other passengers via departure_time)
        if (ride_chairlift(&res, &data, departure_time) == -1) {
            // Note: SEM_CHAIRS is managed by lower_worker/upper_worker, not tourist
            break;
        }

        // Arrive at upper platform (pass chair info for atomic SEM_CHAIRS release)
        if (arrive_upper(&res, &data, chair_id, tourists_on_chair) == -1) {
            break;
        }

        // Descend trail
        if (descend_trail(&res, &data) == -1) {
            break;
        }

        data.rides_completed++;
        update_stats(&res, &data);

        if (data.kid_count > 0) {
            log_info("TOURIST", "%d + %d kids completed ride #%d",
                     data.id, data.kid_count, data.rides_completed);
        } else {
            log_info("TOURIST", "%d completed ride #%d",
                     data.id, data.rides_completed);
        }

        // Single ticket: one ride only
        if (data.ticket_type == TICKET_SINGLE) {
            log_info("TOURIST", "%d leaving (single ticket used)", data.id);
            break;
        }
    }

    if (data.kid_count > 0) {
        log_info("TOURIST", "%d + %d kids exiting (total rides: %d)",
                 data.id, data.kid_count, data.rides_completed);
    } else {
        log_info("TOURIST", "%d exiting (total rides: %d)",
                 data.id, data.rides_completed);
    }

cleanup_family:
    // Join bike thread if created (returns immediately - thread already exited)
    if (bike_thread_created) {
        pthread_join(bike_thread, NULL);
    }

    // Join kid threads (returns immediately - threads already exited)
    for (int i = 0; i < data.kid_count; i++) {
        pthread_join(kid_threads[i], NULL);
    }

    ipc_detach(&res);
    return 0;
}
