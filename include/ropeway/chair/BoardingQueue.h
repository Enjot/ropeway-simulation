#pragma once

#include <cstdint>
#include <sys/types.h>
#include "tourist/TouristType.h"

/**
 * Boarding queue structures for chair assignment.
 * Owned and managed by LowerWorker (lower station controller).
 *
 * Flow:
 * 1. Tourist enters station -> adds BoardingQueueEntry with their slot count
 * 2. LowerWorker checks if tourist fits on current chair (using slots)
 * 3. If fits: assign chair, tourist boards
 * 4. If doesn't fit: dispatch current chair, assign to next chair
 *
 * Note: Children and bikes are threads within tourist process, not separate queue entries.
 * The 'slots' field represents the total space needed (adult + children + bike).
 */

/**
 * Single entry in the boarding queue.
 * Represents a tourist (and their group) waiting to board a chair.
 */
struct BoardingQueueEntry {
    uint32_t touristId;
    pid_t touristPid;
    uint32_t age;
    TouristType type;
    bool isVip;

    /**
     * Total slots needed on chair:
     * - Pedestrian alone: 1
     * - Cyclist with bike: 2
     * - + 1 for each child
     */
    uint32_t slots;

    // Group info (for logging/reporting)
    uint32_t childCount;    // Number of children with this tourist
    bool hasBike;           // Has a bike (cyclist)

    // Chair assignment (set by LowerWorker)
    int32_t assignedChairId; // Assigned chair ID (-1 if waiting)
    bool readyToBoard;       // True when ready to board

    BoardingQueueEntry()
        : touristId{0}, touristPid{0}, age{0}, type{TouristType::PEDESTRIAN},
          isVip{false}, slots{1}, childCount{0}, hasBike{false},
          assignedChairId{-1}, readyToBoard{false} {
    }
};

/**
 * Queue of tourists waiting to board chairs.
 * Fixed-size array suitable for shared memory.
 */
struct BoardingQueue {
    static constexpr uint32_t MAX_SIZE = 64;

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
