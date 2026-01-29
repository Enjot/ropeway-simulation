#pragma once

/**
 * @brief Ropeway operational state
 */
enum class RopewayState {
    STOPPED,
    RUNNING,
    EMERGENCY_STOP,
    CLOSING
};

/**
 * @brief Convert RopewayState enum to string representation.
 * @param state RopewayState to convert
 */
constexpr const char *toString(const RopewayState state) {
    switch (state) {
        case RopewayState::STOPPED: return "STOPPED";
        case RopewayState::RUNNING: return "RUNNING";
        case RopewayState::EMERGENCY_STOP: return "EMERGENCY_STOP";
        case RopewayState::CLOSING: return "CLOSING";
        default: throw std::invalid_argument("Invalid RopewayState value");
    }
}