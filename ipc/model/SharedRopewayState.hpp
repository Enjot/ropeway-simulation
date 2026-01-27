#pragma once

#include <ctime>

#include "SharedChairPoolState.hpp"
#include "SharedOperationalState.hpp"
#include "SharedStatisticState.hpp"
#include "core/Config.hpp"
#include "ropeway/gate/GateType.hpp"
#include "stats/GatePassage.hpp"

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
     * Register a tourist for ride tracking.
     * Called when tourist buys ticket.
     * @return Record index or -1 if full
     */
    int32_t registerTourist(uint32_t touristId, uint32_t ticketId, uint32_t age,
                            TouristType type, bool isVip, int32_t guardianId = -1) {
        // Allow space for tourists + their children (3x multiplier)
        if (stats.touristRecordCount >= Config::Simulation::MAX_TOURIST_RECORDS) return -1;

        TouristRideRecord &record = stats.touristRecords[stats.touristRecordCount];
        record.touristId = touristId;
        record.ticketId = ticketId;
        record.age = age;
        record.type = type;
        record.isVip = isVip;
        record.guardianId = guardianId;
        record.ridesCompleted = 0;
        record.entryGatePassages = 0;
        record.rideGatePassages = 0;

        return static_cast<int32_t>(stats.touristRecordCount++);
    }

    /**
     * Set guardian ID for a tourist record.
     * Used when pairing a child with a guardian.
     */
    void setGuardianId(uint32_t touristId, int32_t guardianId) {
        int32_t idx = findTouristRecord(touristId);
        if (idx >= 0) {
            stats.touristRecords[idx].guardianId = guardianId;
        }
    }

    /**
     * Find tourist record by ID.
     * @return Record index or -1 if not found
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
     * Log a gate passage and update tourist statistics.
     * @param simTimeSeconds Simulated time as seconds since midnight
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
     * Record a completed ride for a tourist.
     */
    void recordRide(uint32_t touristId) {
        int32_t idx = findTouristRecord(touristId);
        if (idx >= 0) {
            stats.touristRecords[idx].ridesCompleted++;
        }
    }
};
