#pragma once

#include "core/Flags.h"
#include "ropeway/chair/Chair.h"
#include "stats/DailyStatistic.h"
#include "stats/GatePassageLog.h"
#include "stats/TouristRideRecord.h"

/**
 * Simulation statistics and reporting data.
 * Accumulated throughout simulation for daily report generation.
 *
 * OWNERSHIP: Various processes update; main orchestrator reads for report.
 */
struct SharedStatisticsState {
    DailyStatistics dailyStats;
    TouristRideRecord touristRecords[Flags::Simulation::MAX_TOURIST_RECORDS];
    uint32_t touristRecordCount;
    uint32_t nextTouristId; // Counter for generating unique tourist IDs (for spawned children)
    GatePassageLog gateLog;

    SharedStatisticsState()
        : dailyStats{},
          touristRecords{},
          touristRecordCount{0},
          nextTouristId{0},
          gateLog{} {
    }
};