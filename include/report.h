#pragma once

/**
 * @file report.h
 * @brief Final simulation report generation.
 */

#include "ipc/shared_state.h"

/**
 * Print final simulation report to stdout.
 * Shows per-tourist summary and aggregates by ticket type.
 * @param state Shared state with simulation data
 */
void print_report(SharedState *state);
