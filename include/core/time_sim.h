#pragma once

/**
 * @file core/time_sim.h
 * @brief Time simulation and acceleration functions.
 */

#include "ipc/shared_state.h"
#include "core/config.h"

/**
 * @brief Initialize time acceleration in shared state.
 *
 * Called once at startup by main process.
 *
 * @param state Shared memory state to initialize.
 * @param cfg Configuration with time settings.
 */
void time_init(SharedState *state, const Config *cfg);

/**
 * @brief Get current simulated time in minutes from midnight.
 *
 * @param state Shared memory state.
 * @return Simulated minutes from midnight (e.g., 480 = 08:00).
 */
int time_get_sim_minutes(SharedState *state);

/**
 * @brief Get current simulated time in minutes from midnight (with fractional part).
 *
 * Used for calculating simulated seconds in logger.
 *
 * @param state Shared memory state.
 * @return Simulated minutes from midnight as double.
 */
double time_get_sim_minutes_f(SharedState *state);

/**
 * @brief Check if simulation time has ended (past sim_end_minutes).
 *
 * @param state Shared memory state.
 * @return 1 if simulation is over, 0 otherwise.
 */
int time_is_simulation_over(SharedState *state);

/**
 * @brief Check if station is closing (approaching end time).
 *
 * @param state Shared memory state.
 * @return 1 if closing, 0 otherwise.
 */
int time_is_closing(SharedState *state);

/**
 * @brief Convert simulated minutes to real seconds.
 *
 * @param state Shared memory state with time acceleration factor.
 * @param sim_minutes Duration in simulated minutes.
 * @return Duration in real seconds.
 */
double time_sim_to_real_seconds(SharedState *state, int sim_minutes);

/**
 * @brief Sleep for simulated minutes (handles pause via EINTR).
 *
 * @param state Shared memory state.
 * @param sem_id Semaphore ID for EINTR handling.
 * @param sim_minutes Duration to sleep in simulated minutes.
 * @return 0 on success, -1 if simulation should stop.
 */
int time_sleep_sim_minutes(SharedState *state, int sem_id, int sim_minutes);

/**
 * @brief Format current simulated time as HH:MM string.
 *
 * @param state Shared memory state.
 * @param buf Output buffer for formatted time.
 * @param buf_size Size of output buffer.
 */
void time_format(SharedState *state, char *buf, int buf_size);

/**
 * @brief Format minutes from midnight as HH:MM string.
 *
 * @param minutes Minutes from midnight to format.
 * @param buf Output buffer for formatted time.
 * @param buf_size Size of output buffer.
 */
void time_format_minutes(int minutes, char *buf, int buf_size);
