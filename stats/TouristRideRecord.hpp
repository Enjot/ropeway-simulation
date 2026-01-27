#pragma once

#include <cstdint>
#include <ctime>
#include "tourist/TouristType.hpp"

/**
 * Per-tourist tracking record for daily report.
 * Created when tourist registers (buys ticket).
 */
struct TouristRideRecord {
    uint32_t touristId;
    uint32_t ticketId;
    uint32_t age;
    TouristType type;
    bool isVip;

    // Guardian tracking (for children under supervision age)
    int32_t guardianId; // Guardian tourist ID (-1 if none/not needed)

    // Accumulated statistics
    uint32_t ridesCompleted;
    uint32_t entryGatePassages;
    uint32_t rideGatePassages;

    TouristRideRecord()
        : touristId{0}, ticketId{0}, age{0}, type{TouristType::PEDESTRIAN},
          isVip{false}, guardianId{-1}, ridesCompleted{0}, entryGatePassages{0}, rideGatePassages{0} {
    }
};