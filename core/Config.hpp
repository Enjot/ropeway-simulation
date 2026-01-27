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
        constexpr uint32_t MAX_TOURIST_RECORDS{1500}; // Max records for stress testing
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
        constexpr float VIP_CHANCE_PERCENTAGE{0.01}; // 1% as per business requirements
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

    namespace Ticket {
        // Time-based ticket durations (scaled for simulation)
        // Real: Tk1=1h, Tk2=2h, Tk3=4h -> scaled to simulation time
        constexpr uint32_t TK1_DURATION_SEC{1 * 3600 / Simulation::TIME_SCALE}; // ~6 sec in sim
        constexpr uint32_t TK2_DURATION_SEC{2 * 3600 / Simulation::TIME_SCALE}; // ~12 sec in sim
        constexpr uint32_t TK3_DURATION_SEC{4 * 3600 / Simulation::TIME_SCALE}; // ~24 sec in sim
        constexpr uint32_t DAILY_DURATION_SEC{10 * 3600 / Simulation::TIME_SCALE}; // Full day

        // Ticket type distribution (percentages)
        constexpr float SINGLE_USE_CHANCE{0.40f}; // 40%
        constexpr float TK1_CHANCE{0.20f}; // 20%
        constexpr float TK2_CHANCE{0.15f}; // 15%
        constexpr float TK3_CHANCE{0.15f}; // 15%
        // Daily = remaining 10%
    }
}
