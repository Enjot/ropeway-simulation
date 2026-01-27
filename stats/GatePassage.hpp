#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include "ropeway/gate/GateType.hpp"

/**
 * Structure for logging gate passages.
 * Records both real and simulated timestamps.
 */
struct GatePassage {
    uint32_t touristId;
    uint32_t ticketId;
    GateType gateType;      // Entry or ride gate
    uint32_t gateNumber;    // Specific gate number (0-3 for entry, 0-2 for ride)
    time_t timestamp;       // Real timestamp
    uint32_t simTimeSeconds; // Simulated time as seconds since midnight
    bool wasAllowed;

    GatePassage() : touristId{0},
                    ticketId{0},
                    gateType{GateType::ENTRY},
                    gateNumber{0},
                    timestamp{0},
                    simTimeSeconds{0},
                    wasAllowed{false} {
    }

    /** Format simulated time as HH:MM string */
    void formatSimTime(char* buffer) const {
        uint32_t hours = simTimeSeconds / 3600;
        uint32_t minutes = (simTimeSeconds % 3600) / 60;
        snprintf(buffer, 6, "%02u:%02u", hours, minutes);
    }
};
