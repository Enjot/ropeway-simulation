#pragma once

#include <cstdint>
#include <ctime>

/**
 * Structure for daily statistics/report
 */
struct DailyStatistics {
    uint32_t totalTourists;
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

    DailyStatistics() : totalTourists{0},
                        totalRides{0},
                        vipTourists{0},
                        childrenServed{0},
                        seniorsServed{0},
                        cyclistRides{0},
                        pedestrianRides{0},
                        emergencyStops{0},
                        totalRevenueWithDiscounts{0.0},
                        simulationStartTime{0},
                        simulationEndTime{0} {
    }
};
