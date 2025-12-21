#pragma once

#include <ctime>

/**
 * Structure for daily statistics/report
 */
struct DailyStatistics {
    int totalTourists;           // Total tourists today
    int totalRides;              // Total rides completed
    int vipTourists;             // Number of VIP tourists
    int childrenServed;          // Children served
    int seniorsServed;           // Seniors served
    int cyclistRides;            // Rides by cyclists
    int pedestrianRides;         // Rides by pedestrians
    int emergencyStops;          // Number of emergency stops
    double totalRevenue;         // Total revenue (with discounts)
    time_t simulationStart;      // Simulation start time
    time_t simulationEnd;        // Simulation end time

    DailyStatistics() : totalTourists(0), totalRides(0), vipTourists(0),
                        childrenServed(0), seniorsServed(0), cyclistRides(0),
                        pedestrianRides(0), emergencyStops(0), totalRevenue(0.0),
                        simulationStart(0), simulationEnd(0) {}
};
