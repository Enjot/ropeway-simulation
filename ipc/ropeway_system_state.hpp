#pragma once

#include <sys/types.h>
#include <ctime>
#include "common/ropeway_state.hpp"
#include "common/config.hpp"
#include "common/gate_type.hpp"
#include "structures/chair.hpp"
#include "structures/daily_statistic.hpp"
#include "ipc/boarding_queue.hpp"
#include "ipc/reporting_data.hpp"

/**
 * Main shared memory structure for the ropeway simulation.
 *
 * This structure is shared across all processes via System V shared memory.
 * Access must be synchronized using semaphores (SHARED_MEMORY semaphore).
 *
 * OWNERSHIP/RESPONSIBILITY:
 * - Main orchestrator: Creates and initializes, sets opening/closing times
 * - Worker1 (lower station): Manages boardingQueue, assigns chairs
 * - Worker2 (upper station): Monitors arrivals at top
 * - Cashier: Reads acceptingNewTourists flag
 * - Tourists: Update counters, register for tracking, log gate passages
 */
struct RopewaySystemState {

    // ==================== CORE OPERATIONAL STATE ====================
    // Primary ropeway status - read by all, written by workers

    RopewayState state;             // STOPPED, RUNNING, EMERGENCY_STOP, CLOSING
    bool acceptingNewTourists;      // False after closing time (Tk)
    time_t openingTime;             // Tp - simulation start
    time_t closingTime;             // Tk - gates stop accepting

    // ==================== STATION COUNTERS ====================
    // Updated by tourists as they move through the system

    uint32_t touristsInLowerStation;  // After entry gate, before platform
    uint32_t touristsOnPlatform;      // On chairs, in transit
    uint32_t totalRidesToday;         // Cumulative ride count

    // ==================== CHAIR SYSTEM ====================
    // Managed by Worker1 (assignment) and tourists (boarding/disembarking)

    uint32_t chairsInUse;                       // Currently occupied chairs
    Chair chairs[Config::Chair::QUANTITY];      // All 72 chairs

    // ==================== BOARDING QUEUE ====================
    // Owned by Worker1 - tourists waiting to board at lower station

    BoardingQueue boardingQueue;

    // ==================== WORKER COORDINATION ====================
    // PIDs for inter-worker signal communication

    pid_t worker1Pid;  // Lower station controller
    pid_t worker2Pid;  // Upper station controller

    // ==================== REPORTING & STATISTICS ====================
    // Accumulated throughout simulation, used for daily report

    DailyStatistics dailyStats;

    static constexpr uint32_t MAX_TOURIST_RECORDS = 100;
    TouristRideRecord touristRecords[MAX_TOURIST_RECORDS];
    uint32_t touristRecordCount;

    GatePassageLog gateLog;

    // ==================== CONSTRUCTOR ====================

    RopewaySystemState()
        : state{RopewayState::STOPPED},
          acceptingNewTourists{false},
          openingTime{Config::Ropeway::DEFAULT_OPENING_TIME},
          closingTime{Config::Ropeway::DEFAULT_CLOSING_TIME},
          touristsInLowerStation{0},
          touristsOnPlatform{0},
          totalRidesToday{0},
          chairsInUse{0},
          chairs{},
          boardingQueue{},
          worker1Pid{0},
          worker2Pid{0},
          dailyStats{},
          touristRecords{},
          touristRecordCount{0},
          gateLog{} {}

    // ==================== TOURIST TRACKING METHODS ====================

    /**
     * Register a tourist for ride tracking.
     * Called when tourist buys ticket.
     * @return Record index or -1 if full
     */
    int32_t registerTourist(uint32_t touristId, uint32_t ticketId, uint32_t age,
                            TouristType type, bool isVip) {
        if (touristRecordCount >= MAX_TOURIST_RECORDS) return -1;

        TouristRideRecord& record = touristRecords[touristRecordCount];
        record.touristId = touristId;
        record.ticketId = ticketId;
        record.age = age;
        record.type = type;
        record.isVip = isVip;
        record.ridesCompleted = 0;
        record.entryGatePassages = 0;
        record.rideGatePassages = 0;

        return static_cast<int32_t>(touristRecordCount++);
    }

    /**
     * Find tourist record by ID.
     * @return Record index or -1 if not found
     */
    int32_t findTouristRecord(uint32_t touristId) const {
        for (uint32_t i = 0; i < touristRecordCount; ++i) {
            if (touristRecords[i].touristId == touristId) {
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
        gateLog.addEntry(passage);

        // Update tourist record counters
        int32_t idx = findTouristRecord(touristId);
        if (idx >= 0) {
            if (gateType == GateType::ENTRY) {
                touristRecords[idx].entryGatePassages++;
            } else {
                touristRecords[idx].rideGatePassages++;
            }
        }
    }

    /**
     * Record a completed ride for a tourist.
     */
    void recordRide(uint32_t touristId) {
        int32_t idx = findTouristRecord(touristId);
        if (idx >= 0) {
            touristRecords[idx].ridesCompleted++;
        }
    }
};
