/**
 * @file tourist/main.c
 * @brief Tourist process entry point and main ride loop orchestration.
 */

#include "tourist/types.h"
#include "tourist/init.h"
#include "tourist/threads.h"
#include "tourist/lifecycle.h"
#include "tourist/boarding.h"
#include "tourist/movement.h"
#include "tourist/stats.h"
#include "ipc/ipc.h"
#include "core/logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static int g_running = 1;

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

    if (tourist_parse_args(argc, argv, &data) == -1) {
        return 1;
    }

    // Install signal handlers
    tourist_setup_signals(&g_running);

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

    // Initialize logger (VIPs get distinct color)
    logger_init(res.state, data.is_vip ? LOG_VIP : LOG_TOURIST);
    logger_set_debug_enabled(res.state->debug_logs_enabled);

    // Initialize family state (simple data only - no sync primitives)
    family.parent_id = data.id;
    family.kid_count = data.kid_count;
    family.has_bike = (data.type == TOURIST_CYCLIST);
    family.res = &res;

    // Create family threads (kids and bike)
    if (tourist_create_family_threads(&data, &family, kid_data, kid_threads,
                                       &bike_data, &bike_thread, &bike_thread_created) == -1) {
        ipc_detach(&res);
        return 1;
    }

    const char *tag = tourist_get_tag(&data);
    const char *type_names[] = {"walker", "cyclist", "family"};
    const char *type_name = type_names[data.type];
    if (data.kid_count > 0) {
        log_info(tag, "%d arrived (age %d, %s%s) with %d kid(s)",
                 data.id, data.age, type_name, data.is_vip ? ", VIP" : "", data.kid_count);
    } else {
        log_info(tag, "%d arrived (age %d, %s%s)",
                 data.id, data.age, type_name, data.is_vip ? ", VIP" : "");
    }

    // Buy ticket at cashier (parent buys for whole family)
    if (tourist_buy_ticket(&res, &data) == -1) {
        log_info(tag, "%d leaving (no ticket)", data.id);
        goto cleanup_family;
    }

    const char *ticket_names[] = {"SINGLE", "TIME_T1", "TIME_T2", "TIME_T3", "DAILY"};
    if (data.kid_count > 0) {
        log_info(tag, "%d got %s family ticket for %d",
                 data.id, ticket_names[data.ticket_type], 1 + data.kid_count);
    } else {
        log_info(tag, "%d got %s ticket",
                 data.id, ticket_names[data.ticket_type]);
    }

    // Record tourist entry for final report
    tourist_record_entry(&res, &data);

    // Main ride loop
    while (g_running && res.state->running) {
        // Check exit conditions
        if (!tourist_is_ticket_valid(&res, &data)) {
            log_info(tag, "%d leaving (ticket expired)", data.id);
            break;
        }

        if (tourist_is_station_closing(&res)) {
            log_info(tag, "%d leaving (station closing)", data.id);
            break;
        }

        // Enter through entry gate (VIPs skip the queue)
        if (data.is_vip) {
            log_info(tag, "%d skipped gate queue", data.id);
        } else {
            if (sem_wait_pauseable(&res, SEM_ENTRY_GATES, 1) == -1) {
                break;
            }
            log_info(tag, "%d entered through gate", data.id);
        }

        // Enter lower station (wait if full)
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

        // Release entry gate now that we're in station
        if (!data.is_vip) {
            sem_post(res.sem_id, SEM_ENTRY_GATES, 1);
        }

        if (data.kid_count > 0) {
            log_info(tag, "%d + %d kids in lower station (count: %d/%d)",
                     data.id, data.kid_count, count, res.state->station_capacity);
        } else {
            log_info(tag, "%d in lower station (count: %d/%d)",
                     data.id, count, res.state->station_capacity);
        }

        // ~10% chance to leave: before first ride (too scared) or after first ride (was too scary)
        if ((data.rides_completed == 0 || data.rides_completed == 1) && (rand() % 10) == 0) {
            const char *reason = (data.rides_completed == 0) ? "too scared to ride" : "that was too scary";
            if (data.kid_count > 0) {
                log_info(tag, "%d + %d kids leaving lower station (%s)",
                         data.id, data.kid_count, reason);
            } else {
                log_info(tag, "%d leaving lower station (%s)", data.id, reason);
            }
            // Release lower station slots
            if (sem_wait_pauseable(&res, SEM_STATE, 1) == 0) {
                res.state->lower_station_count -= data.station_slots;
                sem_post(res.sem_id, SEM_STATE, 1);
            }
            sem_post(res.sem_id, SEM_LOWER_STATION, data.station_slots);
            break;
        }

        // Wait for platform gate (3 gates on lower platform)
        if (sem_wait_pauseable(&res, SEM_PLATFORM_GATES, 1) == -1) {
            if (sem_wait_pauseable(&res, SEM_STATE, 1) == 0) {
                res.state->lower_station_count -= data.station_slots;
                sem_post(res.sem_id, SEM_STATE, 1);
            }
            sem_post(res.sem_id, SEM_LOWER_STATION, data.station_slots);
            break;
        }

        log_info(tag, "%d passed through platform gate", data.id);

        // Release station slots now that we're past the platform gate
        if (sem_wait_pauseable(&res, SEM_STATE, 1) == 0) {
            res.state->lower_station_count -= data.station_slots;
            sem_post(res.sem_id, SEM_STATE, 1);
        }
        sem_post(res.sem_id, SEM_LOWER_STATION, data.station_slots);

        // Board chair (family boards together)
        time_t departure_time = 0;
        int chair_id = 0;
        int tourists_on_chair = 0;
        if (tourist_board_chair(&res, &data, &departure_time, &chair_id, &tourists_on_chair) == -1) {
            sem_post(res.sem_id, SEM_PLATFORM_GATES, 1);
            break;
        }

        // Release platform gate (now boarding chair)
        sem_post(res.sem_id, SEM_PLATFORM_GATES, 1);

        if (data.kid_count > 0) {
            log_info(tag, "%d + %d kids boarded chairlift", data.id, data.kid_count);
        } else {
            log_info(tag, "%d boarded chairlift", data.id);
        }

        // Ride chairlift (synchronized with other passengers via departure_time)
        if (tourist_ride_chairlift(&res, &data, departure_time, &g_running) == -1) {
            break;
        }

        // Arrive at upper platform (pass chair info for atomic SEM_CHAIRS release)
        if (tourist_arrive_upper(&res, &data, chair_id, tourists_on_chair) == -1) {
            break;
        }

        // Descend trail
        if (tourist_descend_trail(&res, &data, &g_running) == -1) {
            break;
        }

        data.rides_completed++;
        tourist_update_stats(&res, &data);

        if (data.kid_count > 0) {
            log_info(tag, "%d + %d kids completed ride #%d",
                     data.id, data.kid_count, data.rides_completed);
        } else {
            log_info(tag, "%d completed ride #%d",
                     data.id, data.rides_completed);
        }

        // Single ticket: one ride only
        if (data.ticket_type == TICKET_SINGLE) {
            log_info(tag, "%d leaving (single ticket used)", data.id);
            break;
        }
    }

    if (data.kid_count > 0) {
        log_info(tag, "%d + %d kids exiting (total rides: %d)",
                 data.id, data.kid_count, data.rides_completed);
    } else {
        log_info(tag, "%d exiting (total rides: %d)",
                 data.id, data.rides_completed);
    }

cleanup_family:
    tourist_join_family_threads(&data, kid_threads, bike_thread, bike_thread_created);
    ipc_detach(&res);
    return 0;
}
