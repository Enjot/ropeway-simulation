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

}
