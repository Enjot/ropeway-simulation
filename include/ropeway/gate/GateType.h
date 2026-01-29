#pragma once

/**
 * @brief Gate type for passage logging
 */
enum class GateType {
    ENTRY,
    RIDE
};

/**
 * @brief Convert GateType enum to string representation.
 * @param type GateType to convert
 * @return "ENTRY" or "RIDE"
 */
constexpr const char *toString(const GateType type) {
    switch (type) {
        case GateType::ENTRY: return "ENTRY";
        case GateType::RIDE: return "RIDE";
        default: throw std::invalid_argument("Invalid GateType value");
    }
}