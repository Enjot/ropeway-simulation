#pragma once

#include <sys/types.h>
#include "common/ropeway_state.hpp"
#include "common/config.hpp"
#include "structures/chair.hpp"
#include "structures/daily_statistic.hpp"
#include "structures/gate_passage.hpp"
#include "common/tourist_type.hpp"
#include "common/gate_type.hpp"

/**
 * Tourist entry in the boarding queue
 */
struct BoardingQueueEntry {
    uint32_t touristId;
    pid_t touristPid;
    uint32_t age;
    TouristType type;
    int32_t guardianId;         // Guardian tourist ID (-1 if none needed)
    int32_t assignedChairId;    // Assigned chair (-1 if waiting)
    bool needsSupervision;      // Child under 8
    bool isAdult;               // Can supervise children
    uint32_t dependentCount;    // Children currently assigned to this adult
    bool readyToBoard;          // Set by Worker1 when group is ready

    BoardingQueueEntry() : touristId{0}, touristPid{0}, age{0},
                           type{TouristType::PEDESTRIAN}, guardianId{-1},
                           assignedChairId{-1}, needsSupervision{false},
                           isAdult{false}, dependentCount{0}, readyToBoard{false} {}
};

/**
 * Boarding queue managed by Worker1
 */
struct BoardingQueue {
    static constexpr uint32_t MAX_QUEUE_SIZE = 32;

    BoardingQueueEntry entries[MAX_QUEUE_SIZE];
    uint32_t count;
    uint32_t nextChairId;

    BoardingQueue() : entries{}, count{0}, nextChairId{0} {}

    int32_t findTourist(uint32_t touristId) const {
        for (uint32_t i = 0; i < count; ++i) {
            if (entries[i].touristId == touristId) {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    bool addTourist(const BoardingQueueEntry& entry) {
        if (count >= MAX_QUEUE_SIZE) return false;
        entries[count] = entry;
        ++count;
        return true;
    }

    void removeTourist(uint32_t index) {
        if (index >= count) return;
        for (uint32_t i = index; i < count - 1; ++i) {
            entries[i] = entries[i + 1];
        }
        entries[count - 1] = BoardingQueueEntry{};
        --count;
    }
};

/**
 * Per-tourist ride record for daily report
 */
struct TouristRideRecord {
    uint32_t touristId;
    uint32_t ticketId;
    uint32_t age;
    TouristType type;
    bool isVip;
    uint32_t ridesCompleted;
    uint32_t entryGatePassages;
    uint32_t rideGatePassages;

    TouristRideRecord() : touristId{0}, ticketId{0}, age{0},
                          type{TouristType::PEDESTRIAN}, isVip{false},
                          ridesCompleted{0}, entryGatePassages{0}, rideGatePassages{0} {}
};

/**
 * Gate passage log for daily report
 */
struct GatePassageLog {
    static constexpr uint32_t MAX_ENTRIES = 200;

    GatePassage entries[MAX_ENTRIES];
    uint32_t count;

    GatePassageLog() : entries{}, count{0} {}

    bool addEntry(const GatePassage& entry) {
        if (count >= MAX_ENTRIES) return false;
        entries[count] = entry;
        ++count;
        return true;
    }
};

/**
 * Structure for shared memory - ropeway system state
 * This is the main shared state accessible by all processes
 */
struct RopewaySystemState {
    RopewayState state;
    uint32_t touristsInLowerStation;
    uint32_t touristsOnPlatform;
    uint32_t chairsInUse;
    Chair chairs[Config::Chair::QUANTITY];
    BoardingQueue boardingQueue;    // Queue of tourists waiting to board

    // Daily statistics and reporting
    DailyStatistics dailyStats;
    static constexpr uint32_t MAX_TOURIST_RECORDS = 100;
    TouristRideRecord touristRecords[MAX_TOURIST_RECORDS];
    uint32_t touristRecordCount;
    GatePassageLog gateLog;

    time_t openingTime;
    time_t closingTime;
    bool acceptingNewTourists;
    uint32_t totalRidesToday;
    pid_t worker1Pid;
    pid_t worker2Pid;

    RopewaySystemState() : state{RopewayState::STOPPED},
                           touristsInLowerStation{0},
                           touristsOnPlatform{0},
                           chairsInUse{0},
                           chairs{},
                           boardingQueue{},
                           dailyStats{},
                           touristRecords{},
                           touristRecordCount{0},
                           gateLog{},
                           openingTime{Config::Ropeway::DEFAULT_OPENING_TIME},
                           closingTime{Config::Ropeway::DEFAULT_CLOSING_TIME},
                           acceptingNewTourists{false},
                           totalRidesToday{0},
                           worker1Pid{0},
                           worker2Pid{0} {
    }

    /**
     * Register a tourist for tracking
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
     * Find tourist record index
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
     * Log a gate passage
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

        // Update tourist record
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
     * Record a completed ride for a tourist
     */
    void recordRide(uint32_t touristId) {
        int32_t idx = findTouristRecord(touristId);
        if (idx >= 0) {
            touristRecords[idx].ridesCompleted++;
        }
    }
};
