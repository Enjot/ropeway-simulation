#pragma once

/**
 * @file report.h
 * @brief Final simulation report generation.
 */

#include "ipc/shared_state.h"

/**
 * @brief Print final simulation report to stdout.
 *
 * Shows per-tourist summary and aggregates by ticket type.
 *
 * @param state Shared state with simulation data.
 */
void print_report(SharedState *state);

/**
 * @brief Write final simulation report to a file.
 *
 * Writes the same content as print_report() to the specified file.
 *
 * @param state Shared state with simulation data.
 * @param filepath Path to the output file.
 * @return 0 on success, -1 on error.
 */
int write_report_to_file(SharedState *state, const char *filepath);
