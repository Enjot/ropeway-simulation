/**
 * @file tourist/movement.c
 * @brief Chairlift ride and trail descent simulation.
 */

#include "tourist/movement.h"
#include "tourist/init.h"
#include "core/time_sim.h"
#include "core/logger.h"

#include <errno.h>
#include <stdlib.h>

int tourist_pauseable_sleep(IPCResources *res, double real_seconds, int *running_flag) {
    time_t start = time(NULL);
    double remaining = real_seconds;

    while (remaining > 0 && *running_flag && res->state->running) {
        struct timespec ts;
        ts.tv_sec = (time_t)remaining;
        ts.tv_nsec = (long)((remaining - ts.tv_sec) * 1e9);

        int ret = nanosleep(&ts, &ts);

        if (ret == -1 && errno == EINTR) {
            // Check if paused
            // Kernel handles SIGTSTP automatically
            // Recalculate remaining
            remaining = real_seconds - (double)(time(NULL) - start);
        } else {
            break;
        }
    }

    return (*running_flag && res->state->running) ? 0 : -1;
}

int tourist_ride_chairlift(IPCResources *res, TouristData *data,
                           time_t departure_time, int *running_flag) {
    // Calculate expected arrival time
    double travel_seconds = time_sim_to_real_seconds(res->state, res->state->chair_travel_time_sim);
    time_t arrival_time = departure_time + (time_t)travel_seconds;

    // Calculate how long we still need to wait
    time_t now = time(NULL);
    double remaining = (double)(arrival_time - now);

    log_info(tourist_get_tag(data), "%d riding chairlift (%.1f real seconds)",
             data->id, travel_seconds);

    // Only sleep if we haven't already reached arrival time
    if (remaining > 0) {
        return tourist_pauseable_sleep(res, remaining, running_flag);
    }

    return 0;
}

int tourist_descend_trail(IPCResources *res, TouristData *data, int *running_flag) {
    int trail_time_sim;

    if (data->type == TOURIST_CYCLIST) {
        // Cyclists randomly pick a bike trail
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
    } else {
        // Walkers and families walk the trail
        trail_time_sim = res->state->trail_walk_time;
    }

    double trail_seconds = time_sim_to_real_seconds(res->state, trail_time_sim);

    log_info(tourist_get_tag(data), "%d descending trail (%.1f real seconds)",
             data->id, trail_seconds);

    return tourist_pauseable_sleep(res, trail_seconds, running_flag);
}
