#pragma once

#include <sys/types.h>

/**
 * System configuration constants
 */
namespace Config {

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

    namespace Worker {
        constexpr uint32_t NUM_WORKERS{2};
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
        constexpr uint32_t MAIN_LOOP_POLL_US{500000};         // 500ms - orchestrator main loop
        constexpr uint32_t WORKER_LOOP_POLL_US{50000};        // 50ms - worker main loop
        constexpr uint32_t CASHIER_LOOP_POLL_US{10000};       // 10ms - cashier main loop
        constexpr uint32_t TOURIST_LOOP_POLL_US{100000};      // 100ms - tourist main loop
        constexpr uint32_t TICKET_RESPONSE_POLL_US{50000};    // 50ms - ticket response polling
        constexpr uint32_t STOPPED_STATE_IDLE_US{500000};     // 500ms - idle when stopped

        // Simulation delays (simulating real-world time)
        constexpr uint32_t ARRIVAL_DELAY_BASE_US{100000};     // 100ms base arrival delay
        constexpr uint32_t ARRIVAL_DELAY_RANDOM_US{200000};   // 200ms random component
        constexpr uint32_t EXIT_ROUTE_DELAY_BASE_US{100000};  // 100ms base exit delay
        constexpr uint32_t EXIT_ROUTE_DELAY_RANDOM_US{200000};// 200ms random component

        // Process management delays (for cleanup, not sync)
        constexpr uint32_t PROCESS_CLEANUP_WAIT_US{300000};   // 300ms wait for cleanup
        constexpr uint32_t SIGNAL_HANDLER_REAP_US{100000};    // 100ms child reaping interval

        // Ride time scaling (divide real time for simulation)
        constexpr uint32_t RIDE_TIME_SCALE{100};              // Divide ride time by this
    }

}
