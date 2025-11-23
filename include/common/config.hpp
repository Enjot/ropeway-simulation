#pragma once

#include <sys/types.h>

/**
 * System configuration constants
 */

namespace RopewayConfig {
    // ========================================================================
    // CHAIR SYSTEM
    // ========================================================================

    constexpr int TOTAL_CHAIRS = 72;              // Total number of 4-person chairs
    constexpr int MAX_CONCURRENT_CHAIRS = 36;     // Max chairs in operation simultaneously
    constexpr int CHAIR_CAPACITY_SLOTS = 4;       // Each chair has 4 slots

    // Chair occupancy rules (in slots)
    constexpr int CYCLIST_SLOTS = 2;              // Cyclist occupies 2 slots
    constexpr int PEDESTRIAN_SLOTS = 1;           // Pedestrian occupies 1 slot
    constexpr int MAX_CYCLISTS_PER_CHAIR = 2;     // Max 2 cyclists (2*2=4 slots)
    constexpr int MAX_PEDESTRIANS_PER_CHAIR = 4;  // Max 4 pedestrians (4*1=4 slots)

    // ========================================================================
    // GATES AND CAPACITY
    // ========================================================================

    constexpr int NUM_ENTRY_GATES = 4;            // Entry gates
    constexpr int NUM_RIDE_GATES = 3;             // Ride gates
    constexpr int NUM_EXIT_ROUTES = 2;            // Exit routes at the upper station
    constexpr int MAX_TOURISTS_ON_STATION = 50;   // N - max tourists in the lower station area

    // ========================================================================
    // WORKERS
    // ========================================================================

    constexpr int NUM_WORKERS = 2;                // Worker 1 (lower), Worker 2 (upper)

    // ========================================================================
    // DISCOUNTS AND AGE CATEGORIES
    // ========================================================================

    // Discounts
    constexpr double CHILD_DISCOUNT = 0.25;       // 25% for children under 10
    constexpr double SENIOR_DISCOUNT = 0.25;      // 25% for seniors over 65

    // Age categories
    constexpr int SUPERVISION_AGE_LIMIT = 8;      // Children under 8 need supervision
    constexpr int CHILD_DISCOUNT_AGE = 10;        // Children under 10 get discount
    constexpr int ADULT_AGE = 18;                 // Adult age threshold
    constexpr int SENIOR_AGE = 65;                // Seniors over 65 get discount
    constexpr int MAX_CHILDREN_PER_ADULT = 2;     // Max children under 8 per adult

    // ========================================================================
    // VIP
    // ========================================================================

    constexpr double VIP_PERCENTAGE = 0.01;       // Approximately 1% of tourists are VIP

    // ========================================================================
    // OPERATING HOURS
    // ========================================================================

    // Operating hours (in seconds from midnight, configurable)
    constexpr int DEFAULT_OPENING_TIME = 8 * 3600;   // Tp - 8:00 AM
    constexpr int DEFAULT_CLOSING_TIME = 18 * 3600;  // Tk - 6:00 PM
    constexpr int SHUTDOWN_DELAY_MS = 3000;          // 3 seconds after the last tourist

    // ========================================================================
    // TRAIL DESCENT TIMES (T1 < T2 < T3)
    // ========================================================================

    constexpr int TRAIL_TIME_EASY = 180;      // T1 - 3 minutes
    constexpr int TRAIL_TIME_MEDIUM = 300;    // T2 - 5 minutes
    constexpr int TRAIL_TIME_HARD = 420;      // T3 - 7 minutes

    // ========================================================================
    // RIDE DURATION
    // ========================================================================

    constexpr int CHAIR_RIDE_TIME = 300;      // 5 minutes to reach the upper station
    constexpr int CHAIR_LOADING_TIME = 30;    // 30 seconds to load passengers

    // ========================================================================
    // IPC KEYS (for shared memory, semaphores, message queues)
    // ========================================================================

    constexpr key_t SHM_KEY_BASE = 0x1000;
    constexpr key_t SEM_KEY_BASE = 0x2000;
    constexpr key_t MSG_KEY_BASE = 0x3000;
}
