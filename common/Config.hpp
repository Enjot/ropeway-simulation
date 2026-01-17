#pragma once

#include <cstdint>
#include <stdexcept>
#include <sys/types.h>
#include "TrailDifficulty.hpp"

/**
 * System configuration constants
 */
namespace Config {
    namespace Simulation {
        constexpr uint32_t NUM_TOURISTS{500};
        constexpr uint32_t STATION_CAPACITY{50};
        constexpr uint32_t SIMULATION_TIME_S{60};
    }

    namespace Chair {
        constexpr uint32_t QUANTITY{72};
        constexpr uint32_t MAX_CONCURRENT_IN_USE{36};
        constexpr uint32_t SLOTS_PER_CHAIR{4};
        constexpr uint32_t CYCLIST_SLOT_COST{2};
        constexpr uint32_t PEDESTRIAN_SLOT_COST{1};
        constexpr uint32_t MAX_CYCLISTS_PER_CHAIR{2};
        constexpr uint32_t MAX_PEDESTRIANS_PER_CHAIR{4};
        constexpr uint32_t RIDE_TIME_S{300};
        constexpr uint32_t LOADING_TIME_S{30};
    }

    namespace Gate {
        constexpr uint32_t NUM_ENTRY_GATES{4};
        constexpr uint32_t NUM_RIDE_GATES{3};
        constexpr uint32_t NUM_EXIT_ROUTES{2};
        constexpr uint32_t MAX_TOURISTS_ON_STATION{50};
        constexpr uint32_t MAX_CHILDREN_PER_ADULT{2};
    }

    namespace Age {
        constexpr uint32_t SUPERVISION_AGE_LIMIT{8};
        constexpr uint32_t ADULT_AGE_FROM{18};
        constexpr uint32_t SENIOR_AGE_FROM{65};
    }

    namespace Discount {
        constexpr uint32_t CHILD_DISCOUNT_AGE{10};
        constexpr float CHILD_DISCOUNT{0.25};
        constexpr float SENIOR_DISCOUNT{0.25};
    }

    namespace Vip {
        constexpr float VIP_CHANCE_PERCENTAGE{0.01};
    }

    namespace Ropeway {
        constexpr uint32_t DEFAULT_OPENING_TIME{8 * 3600}; // Tp - 8:00 AM
        constexpr uint32_t DEFAULT_CLOSING_TIME{18 * 3600}; // Tk - 6:00 PM
        constexpr uint32_t SHUTDOWN_DELAY_S{3};
    }

    namespace Trail {
        constexpr uint32_t TRAIL_TIME_EASY_S{180}; // T1 - 3 minutes
        constexpr uint32_t TRAIL_TIME_MEDIUM_S{300}; // T2 - 5 minutes
        constexpr uint32_t TRAIL_TIME_HARD_S{420}; // T3 - 7 minutes

        inline uint32_t getTimeSeconds(TrailDifficulty difficulty) {
            switch (difficulty) {
                case TrailDifficulty::EASY: return TRAIL_TIME_EASY_S;
                case TrailDifficulty::MEDIUM: return TRAIL_TIME_MEDIUM_S;
                case TrailDifficulty::HARD: return TRAIL_TIME_HARD_S;
                default: throw std::invalid_argument("Invalid TrailDifficulty value");
            }
        }
    }

    namespace Ipc {
        constexpr key_t SHM_KEY_BASE{0x1000};
        constexpr key_t SEM_KEY_BASE{0x2000};
        constexpr key_t MSG_KEY_BASE{0x3000};
    }

    /**
     * Timing constants for delays and polling intervals (in microseconds)
     * NOTE: usleep() should NOT be used for synchronization - use semaphores instead
     * These are only for simulation delays and non-critical polling
     */
    namespace Timing {
        // Polling intervals for main loops (non-synchronization)
        constexpr uint32_t MAIN_LOOP_POLL_US{500000}; // 500ms - orchestrator main loop

        // Simulation delays (simulating real-world time)
        constexpr uint32_t ARRIVAL_DELAY_BASE_US{5000}; // 5ms base arrival delay
        constexpr uint32_t ARRIVAL_DELAY_RANDOM_US{10000}; // 10ms random component
        constexpr uint32_t EXIT_ROUTE_DELAY_BASE_US{100000}; // 100ms base exit delay
        constexpr uint32_t EXIT_ROUTE_DELAY_RANDOM_US{200000}; // 200ms random component

        // Ride time scaling (divide real time for simulation)
        constexpr uint32_t RIDE_TIME_SCALE{100}; // Divide ride time by this
    }
}
