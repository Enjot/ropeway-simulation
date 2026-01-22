#pragma once

#include <cstdint>
#include <ctime>
#include "enums/TouristType.hpp"
#include "enums/GateType.hpp"
#include "structures/GatePassage.hpp"

/**
 * Reporting and tracking structures for daily statistics.
 * Used by the main orchestrator for generating end-of-day reports.
 *
 * These structures accumulate data throughout the simulation:
 * - TouristRideRecord: Per-tourist ride counts and gate passages
 * - GatePassageLog: Chronological log of all gate passages
 */

/**
 * Per-tourist tracking record for daily report.
 * Created when tourist registers (buys ticket).
 */
struct TouristRideRecord {
    uint32_t touristId;
    uint32_t ticketId;
    uint32_t age;
    TouristType type;
    bool isVip;

    // Accumulated statistics
    uint32_t ridesCompleted;
    uint32_t entryGatePassages;
    uint32_t rideGatePassages;

    TouristRideRecord()
        : touristId{0}, ticketId{0}, age{0}, type{TouristType::PEDESTRIAN},
          isVip{false}, ridesCompleted{0}, entryGatePassages{0}, rideGatePassages{0} {}
};

/**
 * Chronological log of gate passages.
 * Fixed-size circular buffer suitable for shared memory.
 */
struct GatePassageLog {
    static constexpr uint32_t MAX_ENTRIES = 200;

    GatePassage entries[MAX_ENTRIES];
    uint32_t count;

    GatePassageLog() : entries{}, count{0} {}

    /** Add gate passage entry. Returns false if log is full. */
    bool addEntry(const GatePassage& entry) {
        if (count >= MAX_ENTRIES) return false;
        entries[count] = entry;
        ++count;
        return true;
    }
};
