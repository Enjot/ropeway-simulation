#pragma once

/**
 * @file ipc/shared_state.h
 * @brief Shared memory structures for inter-process state management.
 */

#include "constants.h"

// ============================================================================
// Per-Tourist Tracking Entry
// ============================================================================

/**
 * @brief Tracking entry for a single tourist in shared memory.
 */
typedef struct {
    int active;                     // 1 if this slot is in use
    int tourist_id;                 // Tourist ID
    int ticket_type;                // TicketType
    int entry_time_sim;             // Simulated time of first entry (minutes from midnight)
    int total_rides;                // Number of rides completed
    int is_vip;                     // VIP status
    TouristType tourist_type;       // Walker/cyclist
    int kid_count;                  // Number of kids (for family tracking)
} TouristEntry;

// ============================================================================
// Shared Memory Structure
// ============================================================================

/**
 * @brief Main shared memory structure containing simulation state.
 *
 * This structure is shared across all processes and protected by semaphores.
 * The tourist_entries flexible array member MUST BE LAST.
 */
typedef struct {
    // Time management
    time_t real_start_time;         // When simulation started (real time)
    int sim_start_minutes;          // 480 = 08:00 (minutes from midnight)
    int sim_end_minutes;            // 1020 = 17:00 (minutes from midnight)
    double time_acceleration;       // Sim minutes per real second
    int chair_travel_time_sim;      // Simulated minutes for ride

    // Time Server manages this atomically (no locks needed to read)
    int64_t current_sim_time_ms;    // Current simulated time in milliseconds

    // Simulation state (protected by SEM_STATE)
    int running;                    // 0 = shutdown
    int closing;                    // 1 = stop accepting new tourists
    int emergency_stop;             // 1 = chairlift stopped (SIGUSR1)
    int emergency_waiters;          // Count of processes waiting on SEM_EMERGENCY_CLEAR

    // Statistics (protected by SEM_STATS)
    int total_tourists;             // Total tourists spawned
    int total_rides;                // Total rides completed
    int rides_by_ticket[TICKET_COUNT]; // Rides per ticket type
    int tourists_by_ticket[TICKET_COUNT]; // Tourists per ticket type

    // For logging/debugging
    int lower_station_count;        // Current tourists in lower station
    int tourists_on_chairs;         // Current tourists on chairlift

    // Config values (read-only after init)
    int station_capacity;           // Max tourists in lower station
    int tourists_to_generate;       // Total number of tourists to generate
    int tourist_spawn_delay_us;     // Delay between spawns in microseconds (0 = no delay)
    int vip_percentage;             // VIP percentage (0-100)
    int walker_percentage;          // Walker percentage (0-100)

    // Trail times in simulated minutes
    int trail_walk_time;
    int trail_bike_fast_time;
    int trail_bike_medium_time;
    int trail_bike_slow_time;

    // Ticket durations in simulated minutes
    int ticket_t1_duration;
    int ticket_t2_duration;
    int ticket_t3_duration;

    // Danger detection settings
    int danger_probability;         // Probability per check (0-100), 0 = disabled
    int danger_duration_sim;        // Simulated minutes emergency stop lasts

    // Logging settings
    int debug_logs_enabled;         // 1 = show debug logs, 0 = hide debug logs

    // Process IDs for signal handling
    pid_t main_pid;
    pid_t time_server_pid;
    pid_t cashier_pid;
    pid_t lower_worker_pid;
    pid_t upper_worker_pid;
    pid_t generator_pid;

    // Per-tourist tracking (flexible array - MUST BE LAST)
    int max_tracked_tourists;       // Config value for array sizing
    int tourist_entry_count;        // Number of entries used
    TouristEntry tourist_entries[]; // Flexible array member
} SharedState;
