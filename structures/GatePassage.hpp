#pragma once

#include <cstdint>
#include <ctime>
#include "common/GateType.hpp"

/**
 * Structure for logging gate passages
 */
struct GatePassage {
    uint32_t touristId;
    uint32_t ticketId;
    GateType gateType; // Entry or ride gate
    uint32_t gateNumber; // Specific gate number (0-3 for entry, 0-2 for ride)
    time_t timestamp;
    bool wasAllowed;

    GatePassage() : touristId{0},
                    ticketId{0},
                    gateType{GateType::ENTRY},
                    gateNumber{0},
                    timestamp{0},
                    wasAllowed{false} {
    }
};
