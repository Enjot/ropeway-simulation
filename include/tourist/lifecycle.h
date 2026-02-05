#pragma once

/**
 * @file tourist/lifecycle.h
 * @brief Tourist ticket management and lifecycle checks.
 */

#include "tourist/types.h"

/**
 * @brief Buy ticket at cashier.
 *
 * For families, parent buys tickets for all kids (same type).
 *
 * @param res IPC resources
 * @param data Tourist data (updated with ticket info on success)
 * @return 0 on success, -1 if rejected or on error
 */
int tourist_buy_ticket(IPCResources *res, TouristData *data);

/**
 * @brief Check if ticket is still valid.
 *
 * Single-use tickets are valid until used once.
 * Time-based and daily tickets: check expiration.
 *
 * @param res IPC resources
 * @param data Tourist data
 * @return 1 if valid, 0 if expired
 */
int tourist_is_ticket_valid(IPCResources *res, TouristData *data);

/**
 * @brief Check if station is closing.
 *
 * @param res IPC resources
 * @return 1 if closing, 0 if still open
 */
int tourist_is_station_closing(IPCResources *res);

/**
 * @brief Check if tourist is too scared to ride.
 *
 * ~10% chance to be scared before first ride or after first ride.
 * Returns 0 if scared_enabled is disabled in config.
 *
 * @param res IPC resources (for config check)
 * @param data Tourist data
 * @return 1 if too scared, 0 otherwise
 */
int tourist_is_too_scared(IPCResources *res, TouristData *data);

/**
 * @brief Get reason string for scared tourist.
 *
 * @param data Tourist data
 * @return "too scared to ride" or "that was too scary"
 */
const char *tourist_scared_reason(TouristData *data);
