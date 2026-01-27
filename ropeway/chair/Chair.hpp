#pragma once

#include <cstdint>
#include <ctime>
#include "core/Config.hpp"

/**
 * Structure representing a chair
 */
struct Chair {
    uint32_t id; // Unique chair identifier (0-71)
    bool isOccupied; // Whether a chair is currently occupied
    uint32_t numPassengers; // Number of passengers on chair
    int32_t passengerIds[Config::Chair::SLOTS_PER_CHAIR]; // Tourist IDs of passengers (max 4)
    uint32_t slotsUsed; // Number of slots used (0-4)
    time_t departureTime; // When a chair left the lower station
    time_t arrivalTime; // When a chair will arrive at the upper station

    Chair() : id{0},
              isOccupied{false},
              numPassengers{0},
              passengerIds{-1},
              slotsUsed{0},
              departureTime{0},
              arrivalTime{0} {
    }

    [[nodiscard]] constexpr bool hasEnoughSpace(const uint32_t slotsNeeded) const {
        return slotsUsed + slotsNeeded <= Config::Chair::SLOTS_PER_CHAIR;
    }
};