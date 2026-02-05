#define _GNU_SOURCE

/**
 * @file tourist/init.c
 * @brief Tourist initialization and argument parsing.
 */

#include "tourist/init.h"
#include "constants.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

// Global pointer for signal handler (set by tourist_setup_signals)
static int *g_running_ptr = NULL;

/**
 * @brief Signal handler for SIGTERM/SIGINT.
 *
 * @param sig Signal number.
 */
static void signal_handler(int sig) {
    if ((sig == SIGTERM || sig == SIGINT) && g_running_ptr) {
        *g_running_ptr = 0;
    }
}

/**
 * @brief Parse command line arguments and populate tourist data.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param data Tourist data structure to populate.
 * @return 0 on success, -1 on error.
 */
int tourist_parse_args(int argc, char *argv[], TouristData *data) {
    if (argc != 7) {
        fprintf(stderr, "Usage: tourist <id> <age> <type> <vip> <kid_count> <ticket_type>\n");
        return -1;
    }

    data->id = atoi(argv[1]);
    data->age = atoi(argv[2]);
    data->type = atoi(argv[3]);
    data->is_vip = atoi(argv[4]);
    data->kid_count = atoi(argv[5]);
    data->ticket_type = atoi(argv[6]);

    data->rides_completed = 0;
    data->ticket_valid_until = 0;

    // Validate constraints
    if (data->kid_count > 0) {
        if (data->type != TOURIST_FAMILY) {
            fprintf(stderr, "Error: Only family type can have kids\n");
            return -1;
        }
        if (data->age < 26) {
            fprintf(stderr, "Error: Must be 26+ to be a guardian\n");
            return -1;
        }
        if (data->kid_count > MAX_KIDS_PER_ADULT) {
            fprintf(stderr, "Error: Maximum %d kids per adult\n", MAX_KIDS_PER_ADULT);
            return -1;
        }
    }

    // Station capacity: person + kids (bike doesn't take waiting room space)
    data->station_slots = 1 + data->kid_count;

    // Chair capacity: walker=1, cyclist=2 (for bike), plus kids
    int parent_chair = (data->type == TOURIST_CYCLIST) ? 2 : 1;
    data->chair_slots = parent_chair + data->kid_count;

    return 0;
}

/**
 * @brief Get logging tag based on tourist type and VIP status.
 *
 * @param data Tourist data.
 * @return Logging tag string.
 */
const char *tourist_get_tag(const TouristData *data) {
    if (data->is_vip) {
        if (data->type == TOURIST_CYCLIST) return "VIP CYCLIST";
        if (data->type == TOURIST_FAMILY) return "VIP FAMILY";
        return "VIP TOURIST";
    }
    if (data->type == TOURIST_CYCLIST) return "CYCLIST";
    if (data->type == TOURIST_FAMILY) return "FAMILY";
    return "TOURIST";
}

/**
 * @brief Install signal handlers for tourist process.
 *
 * @param running_flag Pointer to running flag to clear on SIGTERM/SIGINT.
 */
void tourist_setup_signals(int *running_flag) {
    g_running_ptr = running_flag;

    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
}
