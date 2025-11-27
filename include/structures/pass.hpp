#pragma once

#include <ctime>
#include "common/enums.hpp"

/**
 * Structure representing a pass/ticket
 */
struct Pass {
    int passId;                  // Unique pass identifier
    PassType type;               // Type of pass
    int ownerId;                 // Tourist ID who owns this pass
    int ridesUsed;               // Number of rides used today
    time_t validFrom;            // Valid from timestamp
    time_t validUntil;           // Valid until timestamp
    bool isActive;               // Whether pass is currently active
    bool isVIP;                  // VIP status

    Pass() : passId(0), type(PassType::SINGLE_USE), ownerId(0),
             ridesUsed(0), validFrom(0), validUntil(0),
             isActive(false), isVIP(false) {}
};
