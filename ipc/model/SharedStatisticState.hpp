#pragma once

#include "core/Config.hpp"
#include "ropeway/chair/Chair.hpp"
#include "stats/DailyStatistic.hpp"
#include "stats/GatePassageLog.hpp"
#include "stats/TouristRideRecord.hpp"

/**
 * Simulation statistics and reporting data.
 * Accumulated throughout simulation for daily report generation.
 *
 * OWNERSHIP: Various processes update; main orchestrator reads for report.
 */
struct SharedStatisticsState {
    DailyStatistics dailyStats;
    TouristRideRecord touristRecords[Config::Simulation::MAX_TOURIST_RECORDS];
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
