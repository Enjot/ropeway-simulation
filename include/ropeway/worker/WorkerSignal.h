#pragma once

/**
 * @brief Worker signal types
 */
enum class WorkerSignal {
    EMERGENCY_STOP,
    READY_TO_START,
    STATION_CLEAR,
    DANGER_DETECTED
};

/**
 * @brief Convert WorkerSignal enum to string representation.
 * @param signal WorkerSignal to convert
 * @return String name of the signal
 */
constexpr const char *toString(const WorkerSignal signal) {
    switch (signal) {
        case WorkerSignal::EMERGENCY_STOP: return "EMERGENCY_STOP";
        case WorkerSignal::READY_TO_START: return "READY_TO_START";
        case WorkerSignal::STATION_CLEAR: return "STATION_CLEAR";
        case WorkerSignal::DANGER_DETECTED: return "DANGER_DETECTED";
        default: throw std::invalid_argument("Invalid WorkerSignal value");
    }
}