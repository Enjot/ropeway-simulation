#pragma once

#include "entrance/TicketName.hpp"

/**
 * Message types for cashier communication
 */
namespace CashierMsgType {
    constexpr long REQUEST = 1; // Tourist -> Cashier requests
    constexpr long RESPONSE_BASE = 1000; // Cashier -> Tourist responses (+ touristId)
}

/**
 * Ticket request from Tourist to Cashier
 */
struct TicketRequest {
    uint32_t touristId;
    uint32_t touristAge;
    TicketType requestedType;
    bool requestVip;

    TicketRequest() : touristId{0},
                      touristAge{0},
                      requestedType{TicketType::SINGLE_USE},
                      requestVip{false} {
    }
};

/**
 * Ticket response from Cashier to Tourist
 */
struct TicketResponse {
    uint32_t touristId;
    bool success;
    uint32_t ticketId;
    TicketType ticketType;
    bool isVip;
    double price;
    double discount;
    time_t validFrom;
    time_t validUntil;
    char message[128];

    TicketResponse() : touristId{0},
                       success{false},
                       ticketId{0},
                       ticketType{TicketType::SINGLE_USE},
                       isVip{false},
                       price{0.0},
                       discount{0.0},
                       validFrom{0},
                       validUntil{0},
                       message{} {
    }
};

/**
 * Ticket pricing configuration
 */
namespace TicketPricing {
    constexpr double SINGLE_USE = 15.0;
    constexpr double TIME_TK1 = 30.0; // 1 hour
    constexpr double TIME_TK2 = 50.0; // 2 hours
    constexpr double TIME_TK3 = 70.0; // 4 hours
    constexpr double DAILY = 100.0;

    // Time durations in seconds
    constexpr uint32_t TK1_DURATION = 1 * 3600; // 1 hour
    constexpr uint32_t TK2_DURATION = 2 * 3600; // 2 hours
    constexpr uint32_t TK3_DURATION = 4 * 3600; // 4 hours

    inline double getPrice(TicketType type) {
        switch (type) {
            case TicketType::SINGLE_USE: return SINGLE_USE;
            case TicketType::TIME_TK1: return TIME_TK1;
            case TicketType::TIME_TK2: return TIME_TK2;
            case TicketType::TIME_TK3: return TIME_TK3;
            case TicketType::DAILY: return DAILY;
            default: return SINGLE_USE;
        }
    }

    inline uint32_t getDuration(TicketType type) {
        switch (type) {
            case TicketType::TIME_TK1: return TK1_DURATION;
            case TicketType::TIME_TK2: return TK2_DURATION;
            case TicketType::TIME_TK3: return TK3_DURATION;
            default: return 0; // Single use and daily don't have fixed duration
        }
    }
}
