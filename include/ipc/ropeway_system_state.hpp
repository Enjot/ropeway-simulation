#pragma once

#include <ctime>
#include <sys/types.h>
#include "common/ropeway_state.hpp"
#include "common/config.hpp"
#include "structures/chair.hpp"

/**
 * Structure for shared memory - ropeway system state
 * This is the main shared state accessible by all processes
 */
struct RopewaySystemState {
    RopewayState state;                          // Current operational state
    int touristsInStation;                       // Current tourists in the lower station area
    int touristsOnPlatform;                      // Tourists waiting on a platform
    int activeChairs;                            // Number of chairs currently in use
    Chair chairs[RopewayConfig::TOTAL_CHAIRS];   // All chairs
    time_t openingTime;                          // Tp - opening time
    time_t closingTime;                          // Tk - closing time
    bool acceptingNewTourists;                   // Whether accepting new entries
    int totalRidesToday;                         // Total rides completed today
    pid_t worker1Pid;                            // Process ID of worker 1
    pid_t worker2Pid;                            // Process ID of worker 2

    RopewaySystemState() : state(RopewayState::STOPPED),
                           touristsInStation(0), touristsOnPlatform(0),
                           activeChairs(0),
                           openingTime(RopewayConfig::DEFAULT_OPENING_TIME),
                           closingTime(RopewayConfig::DEFAULT_CLOSING_TIME),
                           acceptingNewTourists(false), totalRidesToday(0),
                           worker1Pid(0), worker2Pid(0) {}
};
