/**
 * @file time_sim.c
 * @brief Time simulation utilities
 *
 * Time is managed by the Time Server process, which updates SharedState.current_sim_time_ms
 * atomically. Other processes simply read this value - no pause offset calculation needed.
 */

#include "core/time_sim.h"
#include <stdio.h>
#include <time.h>
#include <errno.h>

/**
 * @brief Initialize time simulation state
 *
 * Sets up initial configuration values. The Time Server will handle
 * the actual time tracking and pause offset calculation.
 *
 * @param state Shared state to initialize
 * @param cfg Configuration values
 */
void time_init(SharedState *state, const Config *cfg) {
    state->real_start_time = time(NULL);

    // Calculate start and end in minutes from midnight
    state->sim_start_minutes = cfg->sim_start_hour * 60 + cfg->sim_start_minute;
    state->sim_end_minutes = cfg->sim_end_hour * 60 + cfg->sim_end_minute;

    // Validate simulation_duration_real to prevent division by zero
    if (cfg->simulation_duration_real <= 0) {
        fprintf(stderr, "time_init: WARNING - simulation_duration_real=%d is invalid, using 60\n",
                cfg->simulation_duration_real);
        int sim_duration_minutes = state->sim_end_minutes - state->sim_start_minutes;
        state->time_acceleration = (double)sim_duration_minutes / 60.0;
    } else {
        int sim_duration_minutes = state->sim_end_minutes - state->sim_start_minutes;
        state->time_acceleration = (double)sim_duration_minutes / (double)cfg->simulation_duration_real;
    }

    state->chair_travel_time_sim = cfg->chair_travel_time_sim;

    // Initialize current_sim_time_ms to start time
    state->current_sim_time_ms = (int64_t)state->sim_start_minutes * 60 * 1000;
}

/**
 * @brief Get current simulated time in minutes from midnight
 *
 * Reads the atomic time value maintained by the Time Server.
 *
 * @param state Shared state
 * @return Current simulated time in minutes
 */
int time_get_sim_minutes(SharedState *state) {
    int64_t sim_ms = __atomic_load_n(&state->current_sim_time_ms, __ATOMIC_ACQUIRE);
    return (int)(sim_ms / 60000);  // Convert ms to minutes
}

/**
 * @brief Get current simulated time in minutes with fractional precision
 *
 * Reads the atomic time value maintained by the Time Server.
 * Returns a double for sub-minute precision in logging.
 *
 * @param state Shared state
 * @return Current simulated time in minutes (with fraction)
 */
double time_get_sim_minutes_f(SharedState *state) {
    int64_t sim_ms = __atomic_load_n(&state->current_sim_time_ms, __ATOMIC_ACQUIRE);
    return sim_ms / 60000.0;  // Convert ms to minutes
}

/**
 * @brief Check if simulation time has ended
 *
 * @param state Shared state
 * @return 1 if simulation is over, 0 otherwise
 */
int time_is_simulation_over(SharedState *state) {
    return time_get_sim_minutes(state) >= state->sim_end_minutes;
}

/**
 * @brief Check if station is closing (30 sim minutes before end)
 *
 * @param state Shared state
 * @return 1 if closing, 0 otherwise
 */
int time_is_closing(SharedState *state) {
    int closing_time = state->sim_end_minutes - 30;
    return time_get_sim_minutes(state) >= closing_time;
}

/**
 * @brief Convert simulated minutes to real seconds
 *
 * @param state Shared state (for acceleration factor)
 * @param sim_minutes Simulated minutes to convert
 * @return Equivalent real seconds
 */
double time_sim_to_real_seconds(SharedState *state, int sim_minutes) {
    if (state->time_acceleration <= 0) {
        return (double)sim_minutes;  // Fallback: 1 sim minute = 1 real second
    }
    return (double)sim_minutes / state->time_acceleration;
}

/**
 * @brief Sleep for a specified number of simulated minutes
 *
 * Converts sim minutes to real seconds and sleeps. Handles EINTR from signals.
 * The kernel handles SIGTSTP/SIGCONT automatically - no explicit pause checking needed.
 *
 * @param state Shared state
 * @param sem_id Semaphore ID (unused - kept for API compatibility)
 * @param sim_minutes Simulated minutes to sleep
 * @return 0 on success, -1 if simulation is stopping
 */
int time_sleep_sim_minutes(SharedState *state, int sem_id, int sim_minutes) {
    (void)sem_id;  // Unused - kernel handles pause

    double real_seconds = time_sim_to_real_seconds(state, sim_minutes);

    // Use high-precision clock for accurate timing with accelerated simulation
    struct timespec start_ts;
    if (clock_gettime(CLOCK_MONOTONIC, &start_ts) == -1) {
        perror("time_sleep_sim_minutes: clock_gettime start");
        return -1;
    }
    double start_sec = start_ts.tv_sec + start_ts.tv_nsec / 1e9;
    double remaining = real_seconds;

    while (remaining > 0) {
        // Check if simulation is stopping
        if (!state->running) {
            return -1;
        }

        struct timespec ts;
        ts.tv_sec = (time_t)remaining;
        ts.tv_nsec = (long)((remaining - ts.tv_sec) * 1e9);

        int ret = nanosleep(&ts, &ts);

        if (ret == -1 && errno == EINTR) {
            // Interrupted by signal - recalculate remaining time with high precision
            // Note: If SIGTSTP stopped us, the pause time is NOT counted
            // because nanosleep pauses with the process
            struct timespec now_ts;
            if (clock_gettime(CLOCK_MONOTONIC, &now_ts) == -1) {
                perror("time_sleep_sim_minutes: clock_gettime now");
                return -1;
            }
            double now_sec = now_ts.tv_sec + now_ts.tv_nsec / 1e9;
            remaining = real_seconds - (now_sec - start_sec);
        } else {
            break;  // Sleep completed
        }
    }

    return 0;
}

/**
 * @brief Format current simulated time as HH:MM string
 *
 * @param state Shared state
 * @param buf Buffer to write to
 * @param buf_size Buffer size (minimum 6)
 */
void time_format(SharedState *state, char *buf, int buf_size) {
    int minutes = time_get_sim_minutes(state);
    time_format_minutes(minutes, buf, buf_size);
}

/**
 * @brief Format minutes from midnight as HH:MM string
 *
 * @param minutes Minutes from midnight
 * @param buf Buffer to write to
 * @param buf_size Buffer size (minimum 6)
 */
void time_format_minutes(int minutes, char *buf, int buf_size) {
    if (buf_size < 6) {
        buf[0] = '\0';
        return;
    }

    int hours = minutes / 60;
    int mins = minutes % 60;

    // Clamp values
    if (hours > 23) hours = 23;
    if (hours < 0) hours = 0;
    if (mins < 0) mins = 0;
    if (mins > 59) mins = 59;

    snprintf(buf, buf_size, "%02d:%02d", hours, mins);
}
