#pragma once

#include <sys/types.h>
#include <ctime>
#include "enums/RopewayState.hpp"
#include "../Config.hpp"
#include "enums/GateType.hpp"
#include "structures/Chair.hpp"
#include "structures/DailyStatistic.hpp"
#include "ipc/BoardingQueue.hpp"
#include "ipc/ReportingData.hpp"

// ============================================================================
// SUB-STRUCTURES FOR LOGICAL ORGANIZATION
// ============================================================================

/**
 * Core operational state of the ropeway.
 * Contains primary status flags, timing, and station counters.
 *
 * OWNERSHIP: Main orchestrator initializes; workers update state/counters.
 */
struct RopewayCoreState {
    RopewayState state;             // STOPPED, RUNNING, EMERGENCY_STOP, CLOSING
    bool acceptingNewTourists;      // False after closing time (Tk)
    time_t openingTime;             // Tp - simulation start
    time_t closingTime;             // Tk - gates stop accepting

    uint32_t touristsInLowerStation;  // After entry gate, before platform
    uint32_t touristsOnPlatform;      // On chairs, in transit
    uint32_t totalRidesToday;         // Cumulative ride count

    pid_t lowerWorkerPid;  // Lower station controller
    pid_t upperWorkerPid;  // Upper station controller

    RopewayCoreState()
        : state{RopewayState::STOPPED},
          acceptingNewTourists{false},
          openingTime{0},  // Set by initializeState()
          closingTime{0},  // Set by initializeState()
          touristsInLowerStation{0},
          touristsOnPlatform{0},
          totalRidesToday{0},
          lowerWorkerPid{0},
          upperWorkerPid{0} {}
};

/**
 * Chair management pool.
 * Contains all chairs and the boarding queue.
 *
 * OWNERSHIP: LowerWorker manages assignments; tourists update on board/disembark.
 */
struct ChairPool {
    Chair chairs[Config::Chair::QUANTITY];  // All 72 chairs
    uint32_t chairsInUse;                   // Currently occupied chairs
    BoardingQueue boardingQueue;            // Tourists waiting to board

    ChairPool()
        : chairs{},
          chairsInUse{0},
          boardingQueue{} {}
};

/**
 * Simulation statistics and reporting data.
 * Accumulated throughout simulation for daily report generation.
 *
 * OWNERSHIP: Various processes update; main orchestrator reads for report.
 */
struct SimulationStatistics {

    DailyStatistics dailyStats;
    TouristRideRecord touristRecords[Config::Simulation::MAX_TOURIST_RECORDS];
    uint32_t touristRecordCount;
    uint32_t nextTouristId;  // Counter for generating unique tourist IDs (for spawned children)
    GatePassageLog gateLog;

    SimulationStatistics()
        : dailyStats{},
          touristRecords{},
          touristRecordCount{0},
          nextTouristId{0},
          gateLog{} {}
};

// ============================================================================
// MAIN SHARED MEMORY STRUCTURE
// ============================================================================

/**
 * Main shared memory structure for the ropeway simulation.
 *
 * This structure is shared across all processes via System V shared memory.
 * Access must be synchronized using semaphores (SHARED_MEMORY semaphore).
 *
 * Organized into three logical sections:
 * - core: Operational state, timing, counters, worker PIDs
 * - chairPool: Chair management and boarding queue
 * - stats: Statistics and reporting data
 *
 * OWNERSHIP/RESPONSIBILITY:
 * - Main orchestrator: Creates and initializes, sets opening/closing times
 * - LowerWorker (lower station): Manages chairPool.boardingQueue, assigns chairs
 * - UpperWorker (upper station): Monitors arrivals at top
 * - Cashier: Reads core.acceptingNewTourists flag
 * - Tourists: Update counters, register for tracking, log gate passages
 */
struct RopewaySystemState {

    RopewayCoreState core;
    ChairPool chairPool;
    SimulationStatistics stats;

    RopewaySystemState() = default;

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

        TouristRideRecord& record = stats.touristRecords[stats.touristRecordCount];
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
