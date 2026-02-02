#pragma once

/**
 * @file tourist/boarding.h
 * @brief Platform and chair IPC communication with workers.
 */

#include "tourist/types.h"
#include <time.h>

/**
 * @brief Board chair at lower platform.
 *
 * Sends "ready to board" message to lower worker and waits for boarding confirmation.
 * Handles emergency stop blocking.
 *
 * @param res IPC resources
 * @param data Tourist data
 * @param departure_time_out Receives the chair's departure timestamp for synchronized arrival
 * @param chair_id_out Receives the chair ID for upper worker tracking
 * @param tourists_on_chair_out Receives total tourists on this chair
 * @return 0 on success, -1 on failure or shutdown
 */
int tourist_board_chair(IPCResources *res, TouristData *data, time_t *departure_time_out,
                        int *chair_id_out, int *tourists_on_chair_out);

/**
 * @brief Arrive at upper platform.
 *
 * Waits for exit gate, notifies upper worker of arrival with chair info.
 *
 * @param res IPC resources
 * @param data Tourist data
 * @param chair_id Which chair this tourist arrived on
 * @param tourists_on_chair Total tourists expected from this chair
 * @return 0 on success, -1 on failure
 */
int tourist_arrive_upper(IPCResources *res, TouristData *data,
                         int chair_id, int tourists_on_chair);
