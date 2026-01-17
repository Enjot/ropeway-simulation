#pragma once

#include "common/RopewayState.hpp"
#include "common/TouristType.hpp"
#include "common/TicketName.hpp"
#include "common/TrailDifficulty.hpp"
#include "common/GateType.hpp"
#include "common/WorkerSignal.hpp"
#include "structures/Tourist.hpp"

/**
 * Centralized enum-to-string conversion utilities.
 * All functions are constexpr where possible for compile-time evaluation.
 */
namespace EnumStrings {

    // ==================== RopewayState ====================

    /**
     * Convert RopewayState enum to string representation.
     * Used for logging and reporting.
     */
    constexpr const char* toString(RopewayState state) {
        switch (state) {
            case RopewayState::STOPPED:        return "STOPPED";
            case RopewayState::RUNNING:        return "RUNNING";
            case RopewayState::EMERGENCY_STOP: return "EMERGENCY_STOP";
            case RopewayState::CLOSING:        return "CLOSING";
            default:                           return "UNKNOWN";
        }
    }

    // ==================== TouristState ====================

    /**
     * Convert TouristState enum to string representation.
     * Tourist states track lifecycle from arrival to departure.
     */
    constexpr const char* toString(TouristState state) {
        switch (state) {
            case TouristState::BUYING_TICKET:    return "BUYING_TICKET";
            case TouristState::WAITING_ENTRY:    return "WAITING_ENTRY";
            case TouristState::WAITING_BOARDING: return "WAITING_BOARDING";
            case TouristState::ON_CHAIR:         return "ON_CHAIR";
            case TouristState::AT_TOP:           return "AT_TOP";
            case TouristState::ON_TRAIL:         return "ON_TRAIL";
            case TouristState::FINISHED:         return "FINISHED";
            default:                             return "UNKNOWN";
        }
    }

    // ==================== TouristType ====================

    /**
     * Convert TouristType enum to string representation.
     */
    constexpr const char* toString(TouristType type) {
        switch (type) {
            case TouristType::PEDESTRIAN: return "PEDESTRIAN";
            case TouristType::CYCLIST:    return "CYCLIST";
            default:                      return "UNKNOWN";
        }
    }

    /**
     * Short version for compact output (e.g., in tables).
     */
    constexpr const char* toShortString(TouristType type) {
        switch (type) {
            case TouristType::PEDESTRIAN: return "PED";
            case TouristType::CYCLIST:    return "CYC";
            default:                      return "???";
        }
    }

    // ==================== TicketType ====================

    /**
     * Convert TicketType enum to string representation.
     */
    constexpr const char* toString(TicketType type) {
        switch (type) {
            case TicketType::SINGLE_USE: return "SINGLE_USE";
            case TicketType::TIME_TK1:   return "TIME_TK1";
            case TicketType::TIME_TK2:   return "TIME_TK2";
            case TicketType::TIME_TK3:   return "TIME_TK3";
            case TicketType::DAILY:      return "DAILY";
            default:                     return "UNKNOWN";
        }
    }

    /**
     * Descriptive version with duration info for user-facing output.
     */
    constexpr const char* toDescriptiveString(TicketType type) {
        switch (type) {
            case TicketType::SINGLE_USE: return "SINGLE_USE";
            case TicketType::TIME_TK1:   return "TIME_TK1 (1h)";
            case TicketType::TIME_TK2:   return "TIME_TK2 (2h)";
            case TicketType::TIME_TK3:   return "TIME_TK3 (4h)";
            case TicketType::DAILY:      return "DAILY";
            default:                     return "UNKNOWN";
        }
    }

    // ==================== TrailDifficulty ====================

    /**
     * Convert TrailDifficulty enum to string representation.
     */
    constexpr const char* toString(TrailDifficulty trail) {
        switch (trail) {
            case TrailDifficulty::EASY:   return "EASY";
            case TrailDifficulty::MEDIUM: return "MEDIUM";
            case TrailDifficulty::HARD:   return "HARD";
            default:                      return "UNKNOWN";
        }
    }

    /**
     * Trail code (T1, T2, T3) for compact output.
     */
    constexpr const char* toTrailCode(TrailDifficulty trail) {
        switch (trail) {
            case TrailDifficulty::EASY:   return "T1";
            case TrailDifficulty::MEDIUM: return "T2";
            case TrailDifficulty::HARD:   return "T3";
            default:                      return "T?";
        }
    }

    // ==================== GateType ====================

    /**
     * Convert GateType enum to string representation.
     */
    constexpr const char* toString(GateType type) {
        switch (type) {
            case GateType::ENTRY: return "ENTRY";
            case GateType::RIDE:  return "RIDE";
            default:              return "UNKNOWN";
        }
    }

    // ==================== WorkerSignal ====================

    /**
     * Convert WorkerSignal enum to string representation.
     * Used for logging worker-to-worker communication.
     */
    constexpr const char* toString(WorkerSignal signal) {
        switch (signal) {
            case WorkerSignal::EMERGENCY_STOP:  return "EMERGENCY_STOP";
            case WorkerSignal::READY_TO_START:  return "READY_TO_START";
            case WorkerSignal::STATION_CLEAR:   return "STATION_CLEAR";
            case WorkerSignal::DANGER_DETECTED: return "DANGER_DETECTED";
            default:                            return "UNKNOWN";
        }
    }

} // namespace EnumStrings
