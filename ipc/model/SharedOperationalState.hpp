#pragma once

#include <sys/types.h>
#include <ctime>
#include "enums/RopewayState.hpp"

// ============================================================================
// OPERATIONAL STATE (protected by SHM_OPERATIONAL semaphore)
// ============================================================================

/**
 * Operational state of the ropeway.
 * Contains primary status flags, timing, and station counters.
 * Protected by SHM_OPERATIONAL semaphore.
 *
 * OWNERSHIP: Main orchestrator initializes; workers update state/counters.
 */
struct SharedOperationalState {
    RopewayState state;             // STOPPED, RUNNING, EMERGENCY_STOP, CLOSING
    bool acceptingNewTourists;      // False after closing time (Tk)
    time_t openingTime;             // Tp - simulation start
    time_t closingTime;             // Tk - gates stop accepting

    uint32_t touristsInLowerStation;  // After entry gate, before platform
    uint32_t touristsOnPlatform;      // On chairs, in transit
    uint32_t totalRidesToday;         // Cumulative ride count

    pid_t lowerWorkerPid;  // Lower station controller
    pid_t upperWorkerPid;  // Upper station controller

    SharedOperationalState()
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
