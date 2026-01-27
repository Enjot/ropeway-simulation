#pragma once

#include "tourist/TouristType.h"
#include "ropeway/TrailDifficulty.h"
#include "entrance/TicketName.h"
#include "core/Config.h"
#include <ctime>
#include "tourist/TouristState.h"


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
    TicketType ticketType;
    time_t ticketValidUntil;

    // Supervision
    int32_t guardianId;
    uint32_t dependentCount;
    int32_t dependentIds[Constants::Gate::MAX_CHILDREN_PER_ADULT];

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
                ticketType{TicketType::SINGLE_USE},
                ticketValidUntil{0},
                guardianId{-1},
                dependentCount{0},
                dependentIds{-1, -1},
                preferredTrail{TrailDifficulty::EASY},
                ridesCompleted{0},
                arrivalTime{0},
                lastRideTime{0} {
    }

    /**
     * Check if ticket is still valid (for time-based tickets)
     */
    [[nodiscard]] bool isTicketValid() const noexcept {
        if (!hasTicket) return false;
        if (ticketType == TicketType::SINGLE_USE && ridesCompleted > 0) return false;
        return time(nullptr) < ticketValidUntil;
    }

    /**
     * Check if ticket allows multiple rides
     */
    [[nodiscard]] constexpr bool canRideAgain() const noexcept {
        return ticketType != TicketType::SINGLE_USE;
    }

    /**
     * Check if tourist needs supervision (child under 8)
     */
    [[nodiscard]] constexpr bool needsSupervision() const noexcept {
        return age < Constants::Age::SUPERVISION_AGE_LIMIT;
    }

    /**
     * Check if tourist is an adult (can supervise children)
     */
    [[nodiscard]] constexpr bool isAdult() const noexcept {
        return age >= Constants::Age::ADULT_AGE_FROM;
    }

    /**
     * Get number of chair slots this tourist requires
     */
    [[nodiscard]] constexpr uint32_t getSlotCost() const noexcept {
        return (type == TouristType::CYCLIST)
                   ? Constants::Chair::CYCLIST_SLOT_COST
                   : Constants::Chair::PEDESTRIAN_SLOT_COST;
    }
};
