#pragma once

/**
 * @file tourist/movement.h
 * @brief Chairlift ride and trail descent simulation.
 */

#include "tourist/types.h"
#include <time.h>

/**
 * @brief Pause-aware sleep (handles SIGTSTP during sleep).
 *
 * @param res IPC resources
 * @param real_seconds Duration to sleep in real seconds
 * @param running_flag Pointer to running flag (checked during sleep)
 * @return 0 on success, -1 if simulation should stop
 */
int tourist_pauseable_sleep(IPCResources *res, double real_seconds, int *running_flag);

/**
 * @brief Ride the chairlift (synchronized with other passengers via departure_time).
 *
 * @param res IPC resources
 * @param data Tourist data
 * @param departure_time Real timestamp when chair departed
 * @param running_flag Pointer to running flag
 * @return 0 on success, -1 if simulation should stop
 */
int tourist_ride_chairlift(IPCResources *res, TouristData *data,
                           time_t departure_time, int *running_flag);

/**
 * @brief Walk/bike the trail back down.
 *
 * @param res IPC resources
 * @param data Tourist data (type determines trail selection)
 * @param running_flag Pointer to running flag
 * @return 0 on success, -1 if simulation should stop
 */
int tourist_descend_trail(IPCResources *res, TouristData *data, int *running_flag);
