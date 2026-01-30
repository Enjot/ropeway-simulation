#include "time_sim.h"
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <sys/sem.h>

/**
 * Initialize time simulation state.
 * Issue #9 fix: Added validation for division by zero.
 */
void time_init(SharedState *state, const Config *cfg) {
    state->real_start_time = time(NULL);

    // Calculate start and end in minutes from midnight
    state->sim_start_minutes = cfg->sim_start_hour * 60 + cfg->sim_start_minute;
    state->sim_end_minutes = cfg->sim_end_hour * 60 + cfg->sim_end_minute;

    // Issue #9 fix: Validate simulation_duration_real to prevent division by zero
    if (cfg->simulation_duration_real <= 0) {
        fprintf(stderr, "time_init: WARNING - simulation_duration_real=%d is invalid, using 60\n",
                cfg->simulation_duration_real);
        int sim_duration_minutes = state->sim_end_minutes - state->sim_start_minutes;
        state->time_acceleration = (double)sim_duration_minutes / 60.0;  // Default to 60 seconds
    } else {
        // Calculate time acceleration factor
        // (sim_end - sim_start) minutes of simulation time
        // in simulation_duration_real seconds of real time
        int sim_duration_minutes = state->sim_end_minutes - state->sim_start_minutes;
        state->time_acceleration = (double)sim_duration_minutes / (double)cfg->simulation_duration_real;
    }

    state->chair_travel_time_sim = cfg->chair_travel_time_sim;

    // Initialize pause state
    state->pause_start_time = 0;
    state->total_pause_offset = 0;
    state->paused = 0;
}

int time_get_sim_minutes(SharedState *state) {
    time_t now = time(NULL);

    // Calculate effective elapsed real time (excluding pauses)
    time_t effective_elapsed = (now - state->real_start_time) - state->total_pause_offset;

    // If currently paused, also subtract current pause duration
    if (state->paused && state->pause_start_time > 0) {
        effective_elapsed -= (now - state->pause_start_time);
    }

    // Handle negative elapsed time
    if (effective_elapsed < 0) {
        effective_elapsed = 0;
    }

    // Convert to sim minutes using acceleration
    double sim_elapsed = (double)effective_elapsed * state->time_acceleration;
    return state->sim_start_minutes + (int)sim_elapsed;
}

int time_is_simulation_over(SharedState *state) {
    return time_get_sim_minutes(state) >= state->sim_end_minutes;
}

int time_is_closing(SharedState *state) {
    // Station closes 30 sim minutes before end
    int closing_time = state->sim_end_minutes - 30;
    return time_get_sim_minutes(state) >= closing_time;
}

double time_sim_to_real_seconds(SharedState *state, int sim_minutes) {
    if (state->time_acceleration <= 0) {
        return (double)sim_minutes;  // Fallback: 1 sim minute = 1 real second
    }
    return (double)sim_minutes / state->time_acceleration;
}

// Helper: check pause state and block if paused
static int check_pause(SharedState *state, int sem_id) {
    // Use semaphore to safely read paused flag
    struct sembuf sop = {SEM_STATE, -1, 0};

    while (semop(sem_id, &sop, 1) == -1) {
        if (errno == EINTR) continue;
        return -1;
    }

    int is_paused = state->paused;

    sop.sem_op = 1;  // Release
    while (semop(sem_id, &sop, 1) == -1) {
        if (errno == EINTR) continue;
        return -1;
    }

    if (is_paused) {
        // Block on pause semaphore until SIGCONT
        struct sembuf pause_sop = {SEM_PAUSE, -1, 0};
        while (semop(sem_id, &pause_sop, 1) == -1) {
            if (errno == EINTR) {
                // Check if still paused
                continue;
            }
            return -1;
        }
    }

    return 0;
}

int time_sleep_sim_minutes(SharedState *state, int sem_id, int sim_minutes) {
    double real_seconds = time_sim_to_real_seconds(state, sim_minutes);
    time_t start = time(NULL);
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
            // Interrupted by signal - check if paused
            if (check_pause(state, sem_id) == -1) {
                return -1;
            }
            // Recalculate remaining time
            time_t now = time(NULL);
            remaining = real_seconds - (double)(now - start);
        } else {
            break;  // Sleep completed
        }
    }

    return 0;
}

void time_format(SharedState *state, char *buf, int buf_size) {
    int minutes = time_get_sim_minutes(state);
    time_format_minutes(minutes, buf, buf_size);
}

void time_format_minutes(int minutes, char *buf, int buf_size) {
    if (buf_size < 6) {
        buf[0] = '\0';
        return;
    }

    int hours = minutes / 60;
    int mins = minutes % 60;

    // Clamp hours
    if (hours > 23) hours = 23;
    if (hours < 0) hours = 0;
    if (mins < 0) mins = 0;
    if (mins > 59) mins = 59;

    snprintf(buf, buf_size, "%02d:%02d", hours, mins);
}
