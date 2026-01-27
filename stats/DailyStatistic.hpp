#pragma once

#include <cstdint>
#include <ctime>

/**
 * Record of an emergency stop event
 */
struct EmergencyStopRecord {
    time_t startTime;
    time_t endTime;
    uint32_t initiatorWorkerId;  // 1 or 2
    bool resumed;

    EmergencyStopRecord() : startTime{0}, endTime{0},
                            initiatorWorkerId{0}, resumed{false} {}
};

/**
 * Structure for daily statistics/report
 */
struct DailyStatistics {
    uint32_t totalTourists;
    uint32_t ticketsSold;
    uint32_t totalRides;
    uint32_t vipTourists;
    uint32_t childrenServed;
    uint32_t seniorsServed;
    uint32_t cyclistRides;
    uint32_t pedestrianRides;
    uint32_t emergencyStops;
    double totalRevenueWithDiscounts;
    time_t simulationStartTime;
    time_t simulationEndTime;

    // Emergency stop tracking
    static constexpr uint32_t MAX_EMERGENCY_RECORDS = 10;
    EmergencyStopRecord emergencyRecords[MAX_EMERGENCY_RECORDS];
    uint32_t emergencyRecordCount;
    time_t totalEmergencyDuration;

    DailyStatistics() : totalTourists{0},
                        ticketsSold{0},
                        totalRides{0},
                        vipTourists{0},
                        childrenServed{0},
                        seniorsServed{0},
                        cyclistRides{0},
                        pedestrianRides{0},
                        emergencyStops{0},
                        totalRevenueWithDiscounts{0.0},
                        simulationStartTime{0},
                        simulationEndTime{0},
                        emergencyRecords{},
                        emergencyRecordCount{0},
                        totalEmergencyDuration{0} {
    }

    /**
     * Record start of an emergency stop
     * @return index of the record, or -1 if full
     */
    int32_t recordEmergencyStart(uint32_t workerId) {
        if (emergencyRecordCount >= MAX_EMERGENCY_RECORDS) return -1;

        EmergencyStopRecord& record = emergencyRecords[emergencyRecordCount];
        record.startTime = time(nullptr);
        record.endTime = 0;
        record.initiatorWorkerId = workerId;
        record.resumed = false;

        emergencyStops++;
        return static_cast<int32_t>(emergencyRecordCount++);
    }

    /**
     * Record end of an emergency stop (resume)
     */
    void recordEmergencyEnd(int32_t recordIndex) {
        if (recordIndex < 0 || static_cast<uint32_t>(recordIndex) >= emergencyRecordCount) return;

        EmergencyStopRecord& record = emergencyRecords[recordIndex];
        record.endTime = time(nullptr);
        record.resumed = true;

        if (record.endTime > record.startTime) {
            totalEmergencyDuration += (record.endTime - record.startTime);
        }
    }
};
