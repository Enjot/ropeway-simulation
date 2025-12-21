#pragma once

#include <ctime>

/**
 * Structure representing a chair
 */
struct Chair {
    int chairId;                 // Unique chair identifier (0-71)
    bool isOccupied;             // Whether a chair is currently occupied
    int numPassengers;           // Number of passengers on chair
    int passengerIds[4];         // Tourist IDs of passengers (max 4)
    int slotsUsed;               // Number of slots used (0-4)
    time_t departureTime;        // When a chair left the lower station
    time_t arrivalTime;          // When a chair will arrive at the upper station

    Chair() : chairId(0), isOccupied(false), numPassengers(0),
              slotsUsed(0), departureTime(0), arrivalTime(0) {
        for (int i = 0; i < 4; i++) passengerIds[i] = -1;
    }
};
