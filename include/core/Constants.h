#pragma once

#include <cstdint>

/**
 * @brief Fixed constants defined by project requirements.
 * These values CANNOT be changed - they are specified in the requirements document.
 */
namespace Constants {
    namespace Chair {
        constexpr uint32_t QUANTITY{72}; // "72 4-osobowych krzesełek"
        constexpr uint32_t MAX_CONCURRENT_IN_USE{36}; // "jednocześnie może być zajętych 36"
        constexpr uint32_t SLOTS_PER_CHAIR{4}; // "4-osobowych krzesełek"
        constexpr uint32_t CYCLIST_SLOT_COST{2}; // max 2 cyclists per chair
        constexpr uint32_t PEDESTRIAN_SLOT_COST{1}; // max 4 pedestrians per chair
    }

    namespace Gate {
        constexpr uint32_t NUM_ENTRY_GATES{4}; // "4 bramki wstępu"
        constexpr uint32_t NUM_RIDE_GATES{3}; // "3 bramki jazdy"
        constexpr uint32_t MAX_CHILDREN_PER_ADULT{2}; // "maksymalnie 2 takimi dziećmi"
        constexpr uint32_t EXIT_ROUTE_CAPACITY{4}; // Upper station exit route capacity (concurrent users)
    }

    namespace Age {
        constexpr uint32_t SUPERVISION_AGE_LIMIT{8}; // "poniżej 8 roku życia"
        constexpr uint32_t ADULT_AGE_FROM{18};
        constexpr uint32_t SENIOR_AGE_FROM{65}; // "seniorzy powyżej 65"
    }

    namespace Discount {
        constexpr uint32_t CHILD_DISCOUNT_AGE{10}; // "dzieci poniżej 10 roku"
        constexpr float CHILD_DISCOUNT{0.25f}; // "zniżkę 25%"
        constexpr float SENIOR_DISCOUNT{0.25f}; // "zniżkę 25%"
    }

    namespace Vip {
        constexpr float VIP_CHANCE{0.01f}; // "ok. 1%"
    }

    namespace Ropeway {
        constexpr uint32_t SHUTDOWN_DELAY_SEC{3}; // "po 3 sekundach kolej zostanie wyłączona"
    }

    namespace Delay {
        // Simulation delays only - NOT for IPC synchronization
        constexpr uint32_t EXIT_ROUTE_TRANSITION_US{100000}; // Tourist walking to trail after exit (100ms)
    }

    namespace Queue {
        // Message queue flow control capacities
        constexpr uint32_t CASHIER_QUEUE_CAPACITY{1000}; // Total cashier queue slots
        constexpr uint32_t ENTRY_QUEUE_VIP_SLOTS{50}; // Reserved slots for VIPs (~5% buffer)
        constexpr uint32_t ENTRY_QUEUE_REGULAR_SLOTS{950}; // Slots for regular tourists
    }
}
