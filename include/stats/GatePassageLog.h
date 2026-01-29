#pragma once

#include <cstdint>
#include <ctime>
#include "stats/GatePassage.h"

/**
 * Chronological log of gate passages.
 * Fixed-size circular buffer suitable for shared memory.
 */
struct GatePassageLog {
    static constexpr uint32_t MAX_ENTRIES = 200;

    GatePassage entries[MAX_ENTRIES];
    uint32_t count;

    GatePassageLog() : entries{}, count{0} {
    }

    /**
     * @brief Add a gate passage entry to the log.
     * @param entry Gate passage record to add
     * @return true if added successfully, false if log is full (MAX_ENTRIES=200)
     */
    bool addEntry(const GatePassage &entry) {
        if (count >= MAX_ENTRIES) return false;
        entries[count] = entry;
        ++count;
        return true;
    }
};