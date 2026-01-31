#pragma once

// Configuration structure (loaded at startup)
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
    int tourist_spawn_rate;         // Tourists per second
    int max_concurrent_tourists;    // Max active tourist processes
    int max_tracked_tourists;       // Max tourists to track in report (default 5000)

    // Tourist distribution (percentages 0-100)
    int vip_percentage;
    int walker_percentage;

    // Trail times in simulated minutes
    int trail_walk_time;
    int trail_bike_fast_time;
    int trail_bike_medium_time;
    int trail_bike_slow_time;

    // Ticket durations in simulated minutes
    int ticket_t1_duration;         // Time-based T1
    int ticket_t2_duration;         // Time-based T2
    int ticket_t3_duration;         // Time-based T3
} Config;

// Load configuration from file
// Returns 0 on success, -1 on error
int config_load(const char *path, Config *cfg);

// Initialize config with default values
void config_set_defaults(Config *cfg);

// Validate configuration values
// Returns 0 if valid, -1 if invalid (prints errors)
int config_validate(const Config *cfg);
