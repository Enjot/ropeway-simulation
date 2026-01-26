#pragma once

#include "enums/TouristType.hpp"
#include "enums/TrailDifficulty.hpp"
#include "../Config.hpp"
#include <ctime>
#include "enums/TouristState.hpp"


/**
 * Structure representing a tourist
 */
struct Tourist {
    uint32_t id;
    pid_t pid;
    uint32_t age;
    TouristType type;
    TouristState state;
    bool isVip;
    bool wantsToRide;

    // Ticket information
    uint32_t ticketId;
    bool hasTicket;

    // Supervision
    int32_t guardianId;
    uint32_t dependentCount;
    int32_t dependentIds[Config::Gate::MAX_CHILDREN_PER_ADULT];

    // Cyclist trails
    TrailDifficulty preferredTrail;
    uint32_t ridesCompleted;

    // Reporting
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
     * Get number of chair slots this tourist requires
     */
    [[nodiscard]] constexpr uint32_t getSlotCost() const noexcept {
        return (type == TouristType::CYCLIST)
                   ? Config::Chair::CYCLIST_SLOT_COST
                   : Config::Chair::PEDESTRIAN_SLOT_COST;
    }

    // === PHASE 2: Discount calculations (uncomment when implementing cashier pricing) ===
    /*
    [[nodiscard]] constexpr bool isSenior() const noexcept {
        return age >= Config::Age::SENIOR_AGE_FROM;
    }

    [[nodiscard]] constexpr bool hasChildDiscount() const noexcept {
        return age < Config::Discount::CHILD_DISCOUNT_AGE;
    }

    [[nodiscard]] constexpr bool hasDiscount() const noexcept {
        return hasChildDiscount() || isSenior();
    }

    [[nodiscard]] constexpr float getDiscountRate() const noexcept {
        if (hasChildDiscount()) return Config::Discount::CHILD_DISCOUNT;
        if (isSenior()) return Config::Discount::SENIOR_DISCOUNT;
        return 0.0f;
    }
    */

    // === PHASE 2: Guardian management (uncomment when implementing child supervision) ===
    /*
    [[nodiscard]] constexpr bool canTakeDependent() const noexcept {
        return isAdult() && dependentCount < Config::Gate::MAX_CHILDREN_PER_ADULT;
    }

    bool addDependent(int32_t childId) {
        if (!canTakeDependent()) return false;
        dependentIds[dependentCount] = childId;
        ++dependentCount;
        return true;
    }

    bool removeDependent(int32_t childId) {
        for (uint32_t i = 0; i < dependentCount; ++i) {
            if (dependentIds[i] == childId) {
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
    */
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
    // int32_t guardianId;        // PHASE 2
    // TrailDifficulty preferredTrail;  // PHASE 2
    key_t shmKey;
    key_t semKey;
    key_t msgKey;
};
