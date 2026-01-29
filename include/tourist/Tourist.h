#pragma once

#include "tourist/TouristType.h"
#include "ropeway/TrailDifficulty.h"
#include "entrance/TicketName.h"
#include "core/Config.h"
#include "core/Constants.h"
#include <ctime>
#include "tourist/TouristState.h"

/**
 * Structure representing a tourist and their group (children, bike).
 * Children and bikes are threads within the tourist process, not separate processes.
 */
struct
Tourist {
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

    // Cyclist trails
    TrailDifficulty preferredTrail;
    uint32_t ridesCompleted;

    // Group composition (children are threads, not processes)
    uint32_t childCount;        // Number of children (0, 1, or 2)
    uint32_t childAges[2];      // Ages of children (for ticket pricing)
    bool hasBike;               // Cyclist has a bike (takes extra slot)

    /**
     * Total slots needed on chair.
     * Pedestrian = 1, Cyclist (with bike) = 2
     * Each child = 1 additional slot
     */
    uint32_t slots;

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
                preferredTrail{TrailDifficulty::EASY},
                ridesCompleted{0},
                childCount{0},
                childAges{0, 0},
                hasBike{false},
                slots{1},
                arrivalTime{0},
                lastRideTime{0} {
    }

    /**
     * Calculate and set the slots field based on type and children.
     */
    void calculateSlots() {
        slots = 1; // Base: person takes 1 slot

        // Cyclist with bike takes 2 slots
        if (type == TouristType::CYCLIST && hasBike) {
            slots = 2;
        }

        // Each child takes 1 additional slot
        slots += childCount;
    }

    /**
     * Check if ticket is still valid (for time-based tickets).
     * @param totalPausedSeconds Cumulative seconds the simulation was suspended (Ctrl+Z)
     */
    [[nodiscard]] bool isTicketValid(time_t totalPausedSeconds = 0) const noexcept {
        if (!hasTicket) return false;
        if (ticketType == TicketType::SINGLE_USE && ridesCompleted > 0) return false;
        return (time(nullptr) - totalPausedSeconds) < ticketValidUntil;
    }

    /**
     * Check if ticket allows multiple rides
     */
    [[nodiscard]] constexpr bool canRideAgain() const noexcept {
        return ticketType != TicketType::SINGLE_USE;
    }

    /**
     * Check if tourist is an adult (can have children)
     */
    [[nodiscard]] constexpr bool isAdult() const noexcept {
        return age >= Constants::Age::ADULT_AGE_FROM;
    }

    /**
     * Check if this tourist has a group (children or bike)
     */
    [[nodiscard]] constexpr bool hasGroup() const noexcept {
        return childCount > 0 || hasBike;
    }
};
