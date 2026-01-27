#pragma once

#include <cstdint>
#include <ctime>
#include <cstdio>
#include "core/Config.hpp"

/**
 * Helper for simulated time calculations.
 * Converts real elapsed time to simulated ropeway operating hours.
 */
namespace TimeHelper {

    /**
     * Calculate simulated time from real time.
     * @param simulationStartTime When the simulation started (real time)
     * @return Simulated time as seconds since midnight
     */
    inline uint32_t getSimulatedSeconds(time_t simulationStartTime) {
        time_t now = time(nullptr);
        time_t elapsed = now - simulationStartTime;

        // Scale elapsed time to simulated time
        uint32_t simulatedElapsed = static_cast<uint32_t>(elapsed) * Config::Simulation::TIME_SCALE;

        // Add to opening hour (converted to seconds)
        uint32_t simulatedSeconds = Config::Simulation::OPENING_HOUR * 3600 + simulatedElapsed;

        // Cap at end of day (23:59:59)
        if (simulatedSeconds > 24 * 3600 - 1) {
            simulatedSeconds = 24 * 3600 - 1;
        }

        return simulatedSeconds;
    }

    /**
     * Format simulated time as HH:MM string.
     * @param simulationStartTime When the simulation started
     * @param buffer Output buffer (at least 6 bytes)
     */
    inline void formatTime(time_t simulationStartTime, char* buffer) {
        uint32_t seconds = getSimulatedSeconds(simulationStartTime);
        uint32_t hours = seconds / 3600;
        uint32_t minutes = (seconds % 3600) / 60;
        snprintf(buffer, 6, "%02u:%02u", hours, minutes);
    }

    /**
     * Format simulated time as HH:MM:SS string.
     * @param simulationStartTime When the simulation started
     * @param buffer Output buffer (at least 9 bytes)
     */
    inline void formatTimeFull(time_t simulationStartTime, char* buffer) {
        uint32_t seconds = getSimulatedSeconds(simulationStartTime);
        uint32_t hours = seconds / 3600;
        uint32_t minutes = (seconds % 3600) / 60;
        uint32_t secs = seconds % 60;
        snprintf(buffer, 9, "%02u:%02u:%02u", hours, minutes, secs);
    }

    /**
     * Check if simulated time is past closing hour.
     */
    inline bool isPastClosingTime(time_t simulationStartTime) {
        uint32_t seconds = getSimulatedSeconds(simulationStartTime);
        return seconds >= Config::Simulation::CLOSING_HOUR * 3600;
    }

    /**
     * Get simulated hour (0-23).
     */
    inline uint32_t getSimulatedHour(time_t simulationStartTime) {
        return getSimulatedSeconds(simulationStartTime) / 3600;
    }
}
