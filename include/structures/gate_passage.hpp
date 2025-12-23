#pragma once

#include <ctime>
#include "common/gate_type.hpp"

/**
 * Structure for logging gate passages
 */
struct GatePassage {
    int touristId;               // Tourist ID
    int passId;                  // Pass ID
    GateType gateType;           // Entry or ride gate
    int gateNumber;              // Specific gate number (0-3 for entry, 0-2 for ride)
    time_t timestamp;            // Time of passage
    bool wasAllowed;             // Whether passage was allowed

    GatePassage() : touristId(0), passId(0), gateType(GateType::ENTRY),
                    gateNumber(0), timestamp(0), wasAllowed(false) {}
};
