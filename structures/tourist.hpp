#pragma once

#include <cstdint>
#include <sys/types.h>
#include "common/tourist_type.hpp"
#include "common/trail_difficulty.hpp"
#include "common/config.hpp"

/**
 * Tourist state in the simulation.
 * Simplified 7-state machine representing the tourist lifecycle.
 */
enum class TouristState {
    BUYING_TICKET,      // Arriving and purchasing ticket at cashier
    WAITING_ENTRY,      // Waiting in queue for entry gate
    WAITING_BOARDING,   // On lower station, waiting for chair assignment
    ON_CHAIR,           // Riding on the chairlift
    AT_TOP,             // At the upper station
    ON_TRAIL,           // Cyclist on downhill trail
    FINISHED            // Left the area / simulation complete
};

/**
 * Structure representing a tourist
 */
struct Tourist {
    uint32_t id;
    pid_t pid;                      // Process ID when spawned
    uint32_t age;
    TouristType type;
    TouristState state;
    bool isVip;
    bool wantsToRide;               // Some tourists just walk around

    // Ticket information
    uint32_t ticketId;
    bool hasTicket;

    // Supervision (for children under 8)
    int32_t guardianId;             // ID of adult guardian (-1 if none/not needed)
    uint32_t dependentCount;        // Number of children this adult is supervising
    int32_t dependentIds[Config::Gate::MAX_CHILDREN_PER_ADULT]; // IDs of supervised children

    // Cyclist specific
    TrailDifficulty preferredTrail;
    uint32_t ridesCompleted;

    // Timing
    time_t arrivalTime;
    time_t lastRideTime;

    Tourist() : id{0},
                pid{0},
                age{25},
                type{TouristType::PEDESTRIAN},
                state{TouristState::BUYING_TICKET},
                isVip{false},
                wantsToRide{true},
                ticketId{0},
                hasTicket{false},
                guardianId{-1},
                dependentCount{0},
                dependentIds{-1, -1},
                preferredTrail{TrailDifficulty::EASY},
                ridesCompleted{0},
                arrivalTime{0},
                lastRideTime{0} {
    }

    /**
     * Check if tourist needs supervision (child under 8)
     */
    [[nodiscard]] constexpr bool needsSupervision() const noexcept {
        return age < Config::Age::SUPERVISION_AGE_LIMIT;
    }

    /**
     * Check if tourist is an adult (can supervise children)
     */
    [[nodiscard]] constexpr bool isAdult() const noexcept {
        return age >= Config::Age::ADULT_AGE_FROM;
    }

    /**
     * Check if tourist is a senior (65+)
     */
    [[nodiscard]] constexpr bool isSenior() const noexcept {
        return age >= Config::Age::SENIOR_AGE_FROM;
    }

    /**
     * Check if tourist qualifies for child discount (under 10)
     */
    [[nodiscard]] constexpr bool hasChildDiscount() const noexcept {
        return age < Config::Discount::CHILD_DISCOUNT_AGE;
    }

    /**
     * Check if tourist qualifies for any discount
     */
    [[nodiscard]] constexpr bool hasDiscount() const noexcept {
        return hasChildDiscount() || isSenior();
    }

    /**
     * Get discount percentage (0.0 to 1.0)
     */
    [[nodiscard]] constexpr float getDiscountRate() const noexcept {
        if (hasChildDiscount()) return Config::Discount::CHILD_DISCOUNT;
        if (isSenior()) return Config::Discount::SENIOR_DISCOUNT;
        return 0.0f;
    }

    /**
     * Check if adult can take another dependent
     */
    [[nodiscard]] constexpr bool canTakeDependent() const noexcept {
        return isAdult() && dependentCount < Config::Gate::MAX_CHILDREN_PER_ADULT;
    }

    /**
     * Get number of chair slots this tourist requires
     */
    [[nodiscard]] constexpr uint32_t getSlotCost() const noexcept {
        return (type == TouristType::CYCLIST)
            ? Config::Chair::CYCLIST_SLOT_COST
            : Config::Chair::PEDESTRIAN_SLOT_COST;
    }

    /**
     * Add a dependent child (returns true if successful)
     */
    bool addDependent(int32_t childId) {
        if (!canTakeDependent()) return false;
        dependentIds[dependentCount] = childId;
        ++dependentCount;
        return true;
    }

    /**
     * Remove a dependent child
     */
    bool removeDependent(int32_t childId) {
        for (uint32_t i = 0; i < dependentCount; ++i) {
            if (dependentIds[i] == childId) {
                // Shift remaining dependents
                for (uint32_t j = i; j < dependentCount - 1; ++j) {
                    dependentIds[j] = dependentIds[j + 1];
                }
                dependentIds[dependentCount - 1] = -1;
                --dependentCount;
                return true;
            }
        }
        return false;
    }
};

/**
 * Serialized tourist data for passing via command line args to exec()
 * Contains only the essential data needed to reconstruct a Tourist in child process
 */
struct TouristSpawnData {
    uint32_t id;
    uint32_t age;
    TouristType type;
    bool isVip;
    bool wantsToRide;
    int32_t guardianId;
    TrailDifficulty preferredTrail;
    key_t shmKey;       // Shared memory key for system state
    key_t semKey;       // Semaphore key
    key_t msgKey;       // Message queue key
};
