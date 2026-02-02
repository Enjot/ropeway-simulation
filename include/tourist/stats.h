#pragma once

/**
 * @file tourist/stats.h
 * @brief Statistics recording for final report.
 */

#include "tourist/types.h"

/**
 * @brief Record tourist entry in shared memory for final report.
 *
 * Called once when tourist first enters the system with valid ticket.
 *
 * @param res IPC resources
 * @param data Tourist data
 */
void tourist_record_entry(IPCResources *res, TouristData *data);

/**
 * @brief Update statistics for completed ride.
 *
 * Counts rides for parent and all kids in the family.
 *
 * @param res IPC resources
 * @param data Tourist data
 */
void tourist_update_stats(IPCResources *res, TouristData *data);
