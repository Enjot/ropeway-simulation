#pragma once

#include <sys/types.h>
#include <ctime>
#include "common/RopewayState.hpp"
#include "common/Config.hpp"
#include "common/GateType.hpp"
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

    pid_t worker1Pid;  // Lower station controller
    pid_t worker2Pid;  // Upper station controller

    RopewayCoreState()
        : state{RopewayState::STOPPED},
          acceptingNewTourists{false},
          openingTime{Config::Ropeway::DEFAULT_OPENING_TIME},
          closingTime{Config::Ropeway::DEFAULT_CLOSING_TIME},
          touristsInLowerStation{0},
          touristsOnPlatform{0},
          totalRidesToday{0},
          worker1Pid{0},
          worker2Pid{0} {}
};

/**
 * Chair management pool.
 * Contains all chairs and the boarding queue.
 *
 * OWNERSHIP: Worker1 manages assignments; tourists update on board/disembark.
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
    TouristRideRecord touristRecords[Config::Simulation::NUM_TOURISTS];
    uint32_t touristRecordCount;
    GatePassageLog gateLog;

    SimulationStatistics()
        : dailyStats{},
          touristRecords{},
          touristRecordCount{0},
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
 * - Worker1 (lower station): Manages chairPool.boardingQueue, assigns chairs
 * - Worker2 (upper station): Monitors arrivals at top
 * - Cashier: Reads core.acceptingNewTourists flag
 * - Tourists: Update counters, register for tracking, log gate passages
 */
struct RopewaySystemState {

    RopewayCoreState core;
    ChairPool chairPool;
    SimulationStatistics stats;

    RopewaySystemState()
        : core{},
          chairPool{},
          stats{} {}

    // ==================== TOURIST TRACKING METHODS ====================

    /**
     * Register a tourist for ride tracking.
     * Called when tourist buys ticket.
     * @return Record index or -1 if full
     */
    int32_t registerTourist(uint32_t touristId, uint32_t ticketId, uint32_t age,
                            TouristType type, bool isVip) {
        if (stats.touristRecordCount >= Config::Simulation::NUM_TOURISTS) return -1;

        TouristRideRecord& record = stats.touristRecords[stats.touristRecordCount];
        record.touristId = touristId;
        record.ticketId = ticketId;
        record.age = age;
        record.type = type;
        record.isVip = isVip;
        record.ridesCompleted = 0;
        record.entryGatePassages = 0;
        record.rideGatePassages = 0;

        return static_cast<int32_t>(stats.touristRecordCount++);
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
     */
    void logGatePassage(uint32_t touristId, uint32_t ticketId,
                        GateType gateType, uint32_t gateNumber, bool allowed) {
        GatePassage passage;
        passage.touristId = touristId;
        passage.ticketId = ticketId;
        passage.gateType = gateType;
        passage.gateNumber = gateNumber;
        passage.timestamp = time(nullptr);
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
