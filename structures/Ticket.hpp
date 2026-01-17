#pragma once

#include <cstdint>
#include <ctime>
#include "enums/TicketName.hpp"

/**
 * Structure representing a ticket
 */
struct Ticket {
    uint32_t id;
    TicketType type;
    uint32_t touristId;
    uint32_t ridesUsed;
    time_t validFrom;
    time_t validUntil;
    bool isActive;
    bool isVip;

    Ticket() : id{0},
               type{TicketType::SINGLE_USE},
               touristId{0},
               ridesUsed{0},
               validFrom{0},
               validUntil{0},
               isActive{false},
               isVip{false} {
    }
};
