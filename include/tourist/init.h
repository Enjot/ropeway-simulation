#pragma once

/**
 * @file tourist/init.h
 * @brief Tourist initialization and argument parsing.
 */

#include "tourist/types.h"

/**
 * @brief Parse command line arguments for tourist process.
 *
 * Format: tourist <id> <age> <type> <vip> <kid_count> <ticket_type>
 *
 * @param argc Argument count
 * @param argv Argument values
 * @param data Tourist data to populate
 * @return 0 on success, -1 on error
 */
int tourist_parse_args(int argc, char *argv[], TouristData *data);

/**
 * @brief Get the appropriate logging tag based on tourist type.
 *
 * @param data Tourist data
 * @return "FAMILY" for families, "CYCLIST" for cyclists, "TOURIST" for solo walkers
 */
const char *tourist_get_tag(const TouristData *data);

/**
 * @brief Install signal handlers for tourist process.
 *
 * Sets up SIGTERM and SIGINT handlers for graceful shutdown.
 *
 * @param running_flag Pointer to the running flag to clear on signal
 */
void tourist_setup_signals(int *running_flag);
