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

// Tourist data structure
typedef struct {
    int id;
    int age;
    TouristType type;
    int is_vip;
    TicketType ticket_type;
    int ticket_valid_until;  // Sim minutes
    int rides_completed;
    int slots_needed;        // 1 for walker, 2 for cyclist (includes kids)
    int kid_count;           // Number of kids (0-2)
} TouristData;

// Forward declaration
struct FamilyState;

// Family state for managing kid threads
typedef struct FamilyState {
    pthread_mutex_t mutex;
    pthread_barrier_t stage_barrier;  // Sync at each lifecycle stage

    int parent_id;
    int kid_count;
    int current_stage;                // Current simulation stage
    int should_exit;                  // Flag for coordinated exit

    // Shared IPC resources (inherited from parent)
    IPCResources *res;

    // Ticket info (same for all family members)
    TicketType ticket_type;
    int ticket_valid_until;

    // Per-kid data
    int kid_ids[MAX_KIDS_PER_ADULT];  // Generated IDs for logging
} FamilyState;

// Per-thread data for kid threads
typedef struct {
    int kid_index;           // 0 or 1 for kids
    FamilyState *family;
} KidThreadData;

static int g_running = 1;

static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_running = 0;
    }
}

/**
 * Parse command line arguments for tourist process.
 * Format: tourist <id> <age> <type> <vip> <kid_count>
 */
static int parse_args(int argc, char *argv[], TouristData *data) {
    if (argc != 6) {
        fprintf(stderr, "Usage: tourist <id> <age> <type> <vip> <kid_count>\n");
        return -1;
    }

    data->id = atoi(argv[1]);
    data->age = atoi(argv[2]);
    data->type = atoi(argv[3]);
    data->is_vip = atoi(argv[4]);
    data->kid_count = atoi(argv[5]);

    data->rides_completed = 0;
    data->ticket_type = -1;  // Not yet purchased
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

    // Calculate total slots: walker=1, cyclist=2; kids take 1 each
    int parent_slots = (data->type == TOURIST_CYCLIST) ? 2 : 1;
    data->slots_needed = parent_slots + data->kid_count;

    return 0;
}

// Issue #11 fix: Use consolidated ipc_check_pause() instead of duplicated code
// See ipc.c for implementation - wrapper for consistency
static void check_pause(IPCResources *res) {
    ipc_check_pause(res);
}

/**
 * Kid thread function.
 * Kids simply follow their parent through each stage using barrier synchronization.
 * The parent handles all IPC communication for the family.
 */
static void *kid_thread_func(void *arg) {
    KidThreadData *td = (KidThreadData *)arg;
    FamilyState *family = td->family;
    int kid_idx = td->kid_index;

    // Enable async cancellation so pthread_cancel() works even during barrier_wait
    // pthread_barrier_wait is NOT a cancellation point, so we need this
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    log_info("TOURIST", "Kid %d of tourist %d started",
             family->kid_ids[kid_idx], family->parent_id);

    while (!family->should_exit) {
        // Wait at barrier for parent to complete each stage
        int rc = pthread_barrier_wait(&family->stage_barrier);
        if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD) {
            break;  // Barrier destroyed or error
        }

        if (family->should_exit) break;

        // Kid just follows parent - no independent actions needed
        log_debug("TOURIST", "Kid %d completed stage %d with parent %d",
                  family->kid_ids[kid_idx], family->current_stage, family->parent_id);
    }

    log_info("TOURIST", "Kid %d of tourist %d exiting",
             family->kid_ids[kid_idx], family->parent_id);

    return NULL;
}

/**
 * Synchronize parent with kid threads at stage boundaries.
 * Does nothing if no kids or if shutdown is in progress.
 * Returns 0 on success, -1 if shutdown detected.
 */
static int sync_with_kids(FamilyState *family, int stage_num) {
    if (family->kid_count > 0) {
        // Check if shutting down - don't wait on barrier
        if (!g_running || family->should_exit) {
            return -1;
        }
        family->current_stage = stage_num;
        pthread_barrier_wait(&family->stage_barrier);
    }
    return 0;
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
    request.mtype = 1;  // Any positive value
    request.tourist_id = data->id;
    request.tourist_type = data->type;
    request.age = data->age;
    request.is_vip = data->is_vip;
    request.kid_count = data->kid_count;

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
    msg.slots_needed = data->slots_needed;  // Includes kids for families
    msg.kid_count = data->kid_count;

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
    msg.kid_count = data->kid_count;  // For family logging at upper platform

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

/**
 * Update statistics for completed ride.
 * Counts rides for parent and all kids in the family.
 */
static void update_stats(IPCResources *res, TouristData *data) {
    sem_wait(res->sem_id, SEM_STATS);

    // Count parent ride
    res->state->total_rides++;
    if (data->ticket_type >= 0 && data->ticket_type < TICKET_COUNT) {
        res->state->rides_by_ticket[data->ticket_type]++;
    }

    // Count kid rides (same ticket type as parent)
    for (int i = 0; i < data->kid_count; i++) {
        res->state->total_rides++;
        if (data->ticket_type >= 0 && data->ticket_type < TICKET_COUNT) {
            res->state->rides_by_ticket[data->ticket_type]++;
        }
    }

    sem_post(res->sem_id, SEM_STATS);
}

int main(int argc, char *argv[]) {
    TouristData data;
    FamilyState family;
    pthread_t kid_threads[MAX_KIDS_PER_ADULT];
    KidThreadData kid_data[MAX_KIDS_PER_ADULT];

    memset(&family, 0, sizeof(family));

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

    // Initialize family state
    family.parent_id = data.id;
    family.kid_count = data.kid_count;
    family.res = &res;
    family.should_exit = 0;
    family.current_stage = 0;

    // Generate kid IDs for logging (e.g., parent 5 -> kids 501, 502)
    for (int i = 0; i < data.kid_count; i++) {
        family.kid_ids[i] = data.id * 100 + i + 1;
    }

    // Initialize synchronization primitives for family
    if (data.kid_count > 0) {
        pthread_mutex_init(&family.mutex, NULL);

        // Barrier for parent + all kids
        int barrier_count = 1 + data.kid_count;
        if (pthread_barrier_init(&family.stage_barrier, NULL, barrier_count) != 0) {
            fprintf(stderr, "tourist %d: Failed to create barrier\n", data.id);
            ipc_detach(&res);
            return 1;
        }

        // Spawn kid threads
        for (int i = 0; i < data.kid_count; i++) {
            kid_data[i].kid_index = i;
            kid_data[i].family = &family;

            if (pthread_create(&kid_threads[i], NULL, kid_thread_func, &kid_data[i]) != 0) {
                perror("tourist: pthread_create kid");
                // Clean up already-created threads
                family.should_exit = 1;
                for (int j = 0; j < i; j++) {
                    pthread_barrier_wait(&family.stage_barrier);
                    pthread_join(kid_threads[j], NULL);
                }
                pthread_barrier_destroy(&family.stage_barrier);
                pthread_mutex_destroy(&family.mutex);
                ipc_detach(&res);
                return 1;
            }
        }
    }

    const char *type_name = data.type == TOURIST_WALKER ? "walker" : "cyclist";
    if (data.kid_count > 0) {
        log_info("TOURIST", "Tourist %d arrived (age %d, %s%s) with %d kid(s)",
                 data.id, data.age, type_name, data.is_vip ? ", VIP" : "", data.kid_count);
    } else {
        log_info("TOURIST", "Tourist %d arrived (age %d, %s%s)",
                 data.id, data.age, type_name, data.is_vip ? ", VIP" : "");
    }

    // Buy ticket at cashier (parent buys for whole family)
    if (buy_ticket(&res, &data) == -1) {
        log_info("TOURIST", "Tourist %d leaving (no ticket)", data.id);
        goto cleanup_family;
    }

    // Store ticket info in family state for kids
    family.ticket_type = data.ticket_type;
    family.ticket_valid_until = data.ticket_valid_until;

    const char *ticket_names[] = {"SINGLE", "TIME_T1", "TIME_T2", "TIME_T3", "DAILY"};
    if (data.kid_count > 0) {
        log_debug("TOURIST", "Tourist %d got %s family ticket for %d",
                  data.id, ticket_names[data.ticket_type], 1 + data.kid_count);
    } else {
        log_debug("TOURIST", "Tourist %d got %s ticket",
                  data.id, ticket_names[data.ticket_type]);
    }

    // Main ride loop
    while (g_running && res.state->running) {
        check_pause(&res);
        sync_with_kids(&family, 0);  // Stage 0: Start of ride loop

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
        sync_with_kids(&family, 1);  // Stage 1: Through entry gate

        log_debug("TOURIST", "Tourist %d entered through gate", data.id);

        // Enter lower station (wait if full)
        // Family atomically acquires slots to enforce capacity correctly
        int family_size = 1 + data.kid_count;
        if (sem_wait_n(res.sem_id, SEM_LOWER_STATION, family_size) == -1) {
            sem_post(res.sem_id, SEM_ENTRY_GATES);
            break;
        }

        // Update station count for logging (count whole family)
        sem_wait(res.sem_id, SEM_STATE);
        res.state->lower_station_count += family_size;
        int count = res.state->lower_station_count;
        sem_post(res.sem_id, SEM_STATE);

        // Release entry gate now that we're in station
        sem_post(res.sem_id, SEM_ENTRY_GATES);

        if (data.kid_count > 0) {
            log_debug("TOURIST", "Tourist %d + %d kids in lower station (count: %d/%d)",
                      data.id, data.kid_count, count, res.state->station_capacity);
        } else {
            log_debug("TOURIST", "Tourist %d in lower station (count: %d/%d)",
                      data.id, count, res.state->station_capacity);
        }

        check_pause(&res);
        sync_with_kids(&family, 2);  // Stage 2: In lower station

        // Wait for platform gate (3 gates on lower platform)
        if (sem_wait(res.sem_id, SEM_PLATFORM_GATES) == -1) {
            sem_wait(res.sem_id, SEM_STATE);
            res.state->lower_station_count -= family_size;
            sem_post(res.sem_id, SEM_STATE);
            sem_post_n(res.sem_id, SEM_LOWER_STATION, family_size);
            break;
        }

        log_debug("TOURIST", "Tourist %d passed through platform gate", data.id);
        check_pause(&res);
        sync_with_kids(&family, 3);  // Stage 3: Through platform gate

        // Board chair (family boards together)
        if (board_chair(&res, &data) == -1) {
            // Release platform gate and station slots
            sem_post(res.sem_id, SEM_PLATFORM_GATES);
            sem_wait(res.sem_id, SEM_STATE);
            res.state->lower_station_count -= family_size;
            sem_post(res.sem_id, SEM_STATE);
            sem_post_n(res.sem_id, SEM_LOWER_STATION, family_size);
            break;
        }

        // Release platform gate (now boarding chair)
        sem_post(res.sem_id, SEM_PLATFORM_GATES);

        // Release station slots (now on chair)
        sem_wait(res.sem_id, SEM_STATE);
        res.state->lower_station_count -= family_size;
        sem_post(res.sem_id, SEM_STATE);
        sem_post_n(res.sem_id, SEM_LOWER_STATION, family_size);

        if (data.kid_count > 0) {
            log_info("TOURIST", "Tourist %d + %d kids boarded chairlift", data.id, data.kid_count);
        } else {
            log_info("TOURIST", "Tourist %d boarded chairlift", data.id);
        }
        sync_with_kids(&family, 4);  // Stage 4: Boarded chair

        // Ride chairlift
        if (ride_chairlift(&res, &data) == -1) {
            // Still need to release chair and arrive
            sem_post(res.sem_id, SEM_CHAIRS);
            break;
        }
        sync_with_kids(&family, 5);  // Stage 5: Finished ride

        // Arrive at upper platform
        if (arrive_upper(&res, &data) == -1) {
            break;
        }
        sync_with_kids(&family, 6);  // Stage 6: At upper platform

        // Descend trail
        if (descend_trail(&res, &data) == -1) {
            break;
        }
        sync_with_kids(&family, 7);  // Stage 7: Trail completed

        data.rides_completed++;
        update_stats(&res, &data);

        if (data.kid_count > 0) {
            log_info("TOURIST", "Tourist %d + %d kids completed ride #%d",
                     data.id, data.kid_count, data.rides_completed);
        } else {
            log_info("TOURIST", "Tourist %d completed ride #%d",
                     data.id, data.rides_completed);
        }

        // Single ticket: one ride only
        if (data.ticket_type == TICKET_SINGLE) {
            log_info("TOURIST", "Tourist %d leaving (single ticket used)", data.id);
            break;
        }
    }

    if (data.kid_count > 0) {
        log_info("TOURIST", "Tourist %d + %d kids exiting (total rides: %d)",
                 data.id, data.kid_count, data.rides_completed);
    } else {
        log_info("TOURIST", "Tourist %d exiting (total rides: %d)",
                 data.id, data.rides_completed);
    }

cleanup_family:
    // Signal kids to exit and join threads
    if (data.kid_count > 0) {
        family.should_exit = 1;

        // Cancel kid threads - they might be stuck in barrier_wait
        for (int i = 0; i < data.kid_count; i++) {
            pthread_cancel(kid_threads[i]);
        }

        for (int i = 0; i < data.kid_count; i++) {
            pthread_join(kid_threads[i], NULL);
        }

        pthread_barrier_destroy(&family.stage_barrier);
        pthread_mutex_destroy(&family.mutex);
    }

    ipc_detach(&res);
    return 0;
}
