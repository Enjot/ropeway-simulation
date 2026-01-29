#pragma once

/**
 * Type of the ticket
 */
enum class TicketType {
    SINGLE_USE,
    TIME_TK1,
    TIME_TK2,
    TIME_TK3,
    DAILY
};

/**
 * @brief Convert TicketType enum to string representation.
 * @param type TicketType to convert
 * @return "SINGLE_USE", "TIME_TK1", "TIME_TK2", "TIME_TK3", or "DAILY"
 */
constexpr const char *toString(const TicketType type) {
    switch (type) {
        case TicketType::SINGLE_USE: return "SINGLE_USE";
        case TicketType::TIME_TK1: return "TIME_TK1";
        case TicketType::TIME_TK2: return "TIME_TK2";
        case TicketType::TIME_TK3: return "TIME_TK3";
        case TicketType::DAILY: return "DAILY";
        default: throw std::invalid_argument("Invalid TicketType value");
    }
}