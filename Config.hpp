#pragma once

#include <cstdint>
#include <sys/types.h>

/**
 * @brief System configuration constants
 */
namespace Config {
    namespace Time {
        constexpr uint32_t ONE_MILLISECOND_US{1'000};
        constexpr uint32_t ONE_SECOND_US{1'000 * ONE_MILLISECOND_US};
        constexpr uint32_t ONE_MINUTE_US{60 * ONE_SECOND_US};
        constexpr uint32_t MAIN_LOOP_POLL_US{500 * ONE_MILLISECOND_US};
        constexpr uint32_t ARRIVAL_DELAY_BASE_US{5 * ONE_MILLISECOND_US};
        constexpr uint32_t ARRIVAL_DELAY_RANDOM_US{10 * ONE_MILLISECOND_US};
        constexpr uint32_t EXIT_ROUTE_DELAY_BASE_US{100 * ONE_MILLISECOND_US};
        constexpr uint32_t EXIT_ROUTE_DELAY_RANDOM_US{200 * ONE_MILLISECOND_US};
    }

    namespace Simulation {
        constexpr uint32_t NUM_TOURISTS{10};
        constexpr uint32_t STATION_CAPACITY{20};

        constexpr uint32_t OPENING_HOUR{8};
        constexpr uint32_t CLOSING_HOUR{18};
        constexpr uint32_t OPERATING_HOURS{CLOSING_HOUR - OPENING_HOUR};

        constexpr uint32_t DURATION_US{1 * Time::ONE_MINUTE_US};

        constexpr uint32_t TIME_SCALE{OPERATING_HOURS * 3600 / (DURATION_US / Time::ONE_SECOND_US)};
        constexpr uint32_t ONE_SCALED_MINUTE{Time::ONE_MINUTE_US / TIME_SCALE};
    }

    namespace Chair {
        constexpr uint32_t QUANTITY{72};
        constexpr uint32_t MAX_CONCURRENT_IN_USE{36};
        constexpr uint32_t SLOTS_PER_CHAIR{4};
        constexpr uint32_t CYCLIST_SLOT_COST{2};
        constexpr uint32_t PEDESTRIAN_SLOT_COST{1};
        constexpr uint32_t MAX_CYCLISTS_PER_CHAIR{2};
        constexpr uint32_t RIDE_DURATION_US{10 * Time::ONE_MINUTE_US / Simulation::TIME_SCALE};
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
        constexpr uint32_t SHUTDOWN_DELAY_US{3 * Time::ONE_SECOND_US};
    }

    namespace Trail {
        constexpr uint32_t DURATION_EASY_US{15 * Time::ONE_MINUTE_US / Simulation::TIME_SCALE};
        constexpr uint32_t DURATION_MEDIUM_US{30 * Time::ONE_MINUTE_US / Simulation::TIME_SCALE};
        constexpr uint32_t DURATION_HARD_US{35 * Time::ONE_MINUTE_US / Simulation::TIME_SCALE};
    }

    namespace Logging {
        // Set to false to hide technical/internal logs
        constexpr bool IS_DEBUG_ENABLED{false};
        constexpr bool IS_INFO_ENABLED{true};
        constexpr bool IS_WARN_ENABLED{true};
        constexpr bool IS_ERROR_ENABLED{true};
    }
}
