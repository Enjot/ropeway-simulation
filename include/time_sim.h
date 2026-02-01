#pragma once

#include "types.h"
#include "config.h"

// Initialize time acceleration in shared state
// Called once at startup by main process
void time_init(SharedState *state, const Config *cfg);

// Get current simulated time in minutes from midnight
int time_get_sim_minutes(SharedState *state);

// Get current simulated time in minutes from midnight (with fractional part)
// Used for calculating simulated seconds in logger
double time_get_sim_minutes_f(SharedState *state);

// Check if simulation time has ended (past sim_end_minutes)
int time_is_simulation_over(SharedState *state);

// Check if station is closing (approaching end time)
int time_is_closing(SharedState *state);

// Convert simulated minutes to real seconds
double time_sim_to_real_seconds(SharedState *state, int sim_minutes);

// Sleep for simulated minutes (handles pause via EINTR)
// Returns 0 on success, -1 if simulation should stop
int time_sleep_sim_minutes(SharedState *state, int sem_id, int sim_minutes);

// Format simulated time as HH:MM string
void time_format(SharedState *state, char *buf, int buf_size);

// Format minutes from midnight as HH:MM string
void time_format_minutes(int minutes, char *buf, int buf_size);
