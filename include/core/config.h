#pragma once

/**
 * @file core/config.h
 * @brief Configuration loading and validation.
 */

/**
 * @brief Configuration structure (loaded at startup).
 */
typedef struct {
    // Station settings
    int station_capacity;           // Max tourists in lower station

    // Time settings
    int simulation_duration_real;   // Real seconds for simulation
    int sim_start_hour;             // Start hour (e.g., 8)
    int sim_start_minute;           // Start minute (e.g., 0)
    int sim_end_hour;               // End hour (e.g., 17)
    int sim_end_minute;             // End minute (e.g., 0)
    int chair_travel_time_sim;      // Simulated minutes for chair ride

    // Tourist generation
    int total_tourists;             // Total number of tourists to generate
    int tourist_spawn_delay_us;     // Delay between spawns in microseconds (0 = no delay)

    // Tourist distribution (percentages 0-100)
    int vip_percentage;
    int walker_percentage;
    int family_percentage;          // Percentage of eligible walkers (26+) who become families

    // Trail times in simulated minutes
    int trail_walk_time;
    int trail_bike_fast_time;
    int trail_bike_medium_time;
    int trail_bike_slow_time;

    // Ticket durations in simulated minutes
    int ticket_t1_duration;         // Time-based T1
    int ticket_t2_duration;         // Time-based T2
    int ticket_t3_duration;         // Time-based T3

    // Danger detection settings
    int danger_probability;         // Probability per check (0-100), 0 = disabled
    int danger_duration_sim;        // Simulated minutes emergency stop lasts

    // Logging settings
    int debug_logs_enabled;         // 1 = show debug logs, 0 = hide debug logs

    // Tourist behavior settings
    int scared_enabled;             // 1 = tourists can be scared, 0 = disabled
} Config;

/**
 * @brief Load configuration from a file.
 *
 * @param path Path to the configuration file.
 * @param cfg Configuration structure to populate.
 * @return 0 on success, -1 on error.
 */
int config_load(const char *path, Config *cfg);

/**
 * @brief Initialize configuration with default values.
 *
 * @param cfg Configuration structure to initialize.
 */
void config_set_defaults(Config *cfg);

/**
 * @brief Validate configuration values.
 *
 * Prints error messages for invalid values to stderr.
 *
 * @param cfg Configuration structure to validate.
 * @return 0 if valid, -1 if invalid.
 */
int config_validate(const Config *cfg);
