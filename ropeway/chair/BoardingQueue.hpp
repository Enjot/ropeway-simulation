#pragma once

#include <cstdint>
#include <sys/types.h>
#include "tourist/TouristType.hpp"

/**
 * Boarding queue structures for chair assignment.
 * Owned and managed by LowerWorker (lower station controller).
 *
 * Flow:
 * 1. Tourist enters station -> adds BoardingQueueEntry
 * 2. LowerWorker pairs children with guardians
 * 3. LowerWorker assigns tourists to chairs (sets assignedChairId, readyToBoard)
 * 4. Tourist boards chair -> removed from queue
 */

/**
 * Single entry in the boarding queue.
 * Represents a tourist waiting to board a chair.
 */
struct BoardingQueueEntry {
    uint32_t touristId;
    pid_t touristPid;
    uint32_t age;
    TouristType type;

    // Guardian/supervision tracking (for children under 8)
    int32_t guardianId; // Guardian tourist ID (-1 if none needed)
    bool needsSupervision; // True if child under 8
    bool isAdult; // True if can supervise children
    uint32_t dependentCount; // Children currently assigned to this adult

    // Chair assignment (set by LowerWorker)
    int32_t assignedChairId; // Assigned chair ID (-1 if waiting)
    bool readyToBoard; // True when group is ready to board

    BoardingQueueEntry()
        : touristId{0}, touristPid{0}, age{0}, type{TouristType::PEDESTRIAN},
          guardianId{-1}, needsSupervision{false}, isAdult{false}, dependentCount{0},
          assignedChairId{-1}, readyToBoard{false} {
    }
};

/**
 * Queue of tourists waiting to board chairs.
 * Fixed-size array suitable for shared memory.
 */
struct BoardingQueue {
    static constexpr uint32_t MAX_SIZE = 32;

    BoardingQueueEntry entries[MAX_SIZE];
    uint32_t count;
    uint32_t nextChairId; // Round-robin chair assignment

    BoardingQueue() : entries{}, count{0}, nextChairId{0} {
    }

    /** Find tourist by ID. Returns index or -1 if not found. */
    int32_t findTourist(uint32_t touristId) const {
        for (uint32_t i = 0; i < count; ++i) {
            if (entries[i].touristId == touristId) {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    /** Add tourist to queue. Returns false if queue is full. */
    bool addTourist(const BoardingQueueEntry &entry) {
        if (count >= MAX_SIZE) return false;
        entries[count] = entry;
        ++count;
        return true;
    }

    /** Remove tourist at index, shifting remaining entries. */
    void removeTourist(uint32_t index) {
        if (index >= count) return;
        for (uint32_t i = index; i < count - 1; ++i) {
            entries[i] = entries[i + 1];
        }
        entries[count - 1] = BoardingQueueEntry{};
        --count;
    }
};