#pragma once

/**
 * Worker signal types
 */
enum class WorkerSignal {
    EMERGENCY_STOP,  // Signal 1 - emergency stop
    READY_TO_START,  // Signal 2 - ready-to-resume operation
    STATION_CLEAR,   // Station is clear and safe
    DANGER_DETECTED  // Danger detected
};
