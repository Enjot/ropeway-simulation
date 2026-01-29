#pragma once

#include <ctime>

#include "SharedChairPoolState.h"
#include "SharedOperationalState.h"
#include "SharedStatisticState.h"
#include "core/Flags.h"
#include "ropeway/gate/GateType.h"
#include "stats/GatePassage.h"

// ============================================================================
// MAIN SHARED MEMORY STRUCTURE
// ============================================================================

/**
 * Main shared memory structure for the ropeway simulation.
 *
 * This structure is shared across all processes via System V shared memory.
 * Access must be synchronized using fine-grained semaphores:
 * - SHM_OPERATIONAL: Protects 'operational' section (state, counters, PIDs)
 * - SHM_CHAIRS: Protects 'chairPool' section (chairs, boarding queue)
 * - SHM_STATS: Protects 'stats' section (statistics, records, gate log)
 *
 * Lock ordering (to prevent deadlocks): SHM_OPERATIONAL -> SHM_CHAIRS -> SHM_STATS
 *
 * Organized into three logical sections:
 * - operational: Ropeway state, timing, counters, worker PIDs
 * - chairPool: Chair management and boarding queue
 * - stats: Statistics and reporting data
 *
 * OWNERSHIP/RESPONSIBILITY:
 * - Main orchestrator: Creates and initializes, sets opening/closing times
 * - LowerWorker (lower station): Manages chairPool.boardingQueue, assigns chairs
 * - UpperWorker (upper station): Monitors arrivals at top
 * - Cashier: Reads operational.acceptingNewTourists flag
 * - Tourists: Update counters, register for tracking, log gate passages
 */
struct SharedRopewayState {
    SharedOperationalState operational;
    SharedChairPoolState chairPool;
    SharedStatisticsState stats;

    SharedRopewayState() = default;

    // ==================== TOURIST TRACKING METHODS ====================

    /**
     * @brief Register a tourist for ride tracking.
     * @param touristId Unique tourist ID
     * @param ticketId Assigned ticket ID
     * @param age Tourist age
     * @param type Tourist type (PEDESTRIAN or CYCLIST)
     * @param isVip VIP status
     * @param guardianId Guardian's tourist ID (-1 if none)
     * @param childCount Number of children with this tourist
     * @return Record index in touristRecords array, or -1 if array is full
     *
     * Called when tourist purchases ticket to start tracking their rides.
     */
    int32_t registerTourist(uint32_t touristId, uint32_t ticketId, uint32_t age,
                            TouristType type, bool isVip, int32_t guardianId = -1,
                            uint32_t childCount = 0) {
        // Allow space for tourists + their children (3x multiplier)
        if (stats.touristRecordCount >= Flags::Simulation::MAX_TOURIST_RECORDS) return -1;

        TouristRideRecord &record = stats.touristRecords[stats.touristRecordCount];
        record.touristId = touristId;
        record.ticketId = ticketId;
        record.age = age;
        record.type = type;
        record.isVip = isVip;
        record.guardianId = guardianId;
        record.childCount = childCount;
        record.ridesCompleted = 0;
        record.entryGatePassages = 0;
        record.rideGatePassages = 0;

        return static_cast<int32_t>(stats.touristRecordCount++);
    }

    /**
     * @brief Set guardian ID for a tourist record.
     * @param touristId Tourist ID to update
     * @param guardianId Guardian's tourist ID
     *
     * Used to link children with their supervising adult.
     */
    void setGuardianId(uint32_t touristId, int32_t guardianId) {
        int32_t idx = findTouristRecord(touristId);
        if (idx >= 0) {
            stats.touristRecords[idx].guardianId = guardianId;
        }
    }

    /**
     * @brief Find tourist record by ID.
     * @param touristId Tourist ID to search for
     * @return Index in touristRecords array, or -1 if not found
     */
    int32_t findTouristRecord(uint32_t touristId) const {
        for (uint32_t i = 0; i < stats.touristRecordCount; ++i) {
            if (stats.touristRecords[i].touristId == touristId) {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    /**
     * @brief Log a gate passage and update tourist statistics.
     * @param touristId Tourist ID
     * @param ticketId Ticket ID
     * @param gateType Type of gate (ENTRY or RIDE)
     * @param gateNumber Gate number
     * @param allowed Whether passage was allowed
     * @param simTimeSeconds Simulated time as seconds since midnight
     *
     * Records the passage in gateLog and updates the tourist's passage counters.
     */
    void logGatePassage(uint32_t touristId, uint32_t ticketId,
                        GateType gateType, uint32_t gateNumber, bool allowed,
                        uint32_t simTimeSeconds = 0) {
        GatePassage passage;
        passage.touristId = touristId;
        passage.ticketId = ticketId;
        passage.gateType = gateType;
        passage.gateNumber = gateNumber;
        passage.timestamp = time(nullptr);
        passage.simTimeSeconds = simTimeSeconds;
        passage.wasAllowed = allowed;
        stats.gateLog.addEntry(passage);

        // Update tourist record counters
        int32_t idx = findTouristRecord(touristId);
        if (idx >= 0) {
            if (gateType == GateType::ENTRY) {
                stats.touristRecords[idx].entryGatePassages++;
            } else {
                stats.touristRecords[idx].rideGatePassages++;
            }
        }
    }

    /**
     * @brief Record a completed ride for a tourist.
     * @param touristId Tourist ID
     *
     * Increments the ridesCompleted counter in the tourist's record.
     */
    void recordRide(uint32_t touristId) {
        int32_t idx = findTouristRecord(touristId);
        if (idx >= 0) {
            stats.touristRecords[idx].ridesCompleted++;
        }
    }
};