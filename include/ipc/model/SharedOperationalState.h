#pragma once

#include <sys/types.h>
#include <ctime>
#include "core/RopewayState.h"

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
    RopewayState state; // STOPPED, RUNNING, EMERGENCY_STOP, CLOSING
    bool acceptingNewTourists; // False after closing time (Tk)
    time_t openingTime; // Tp - simulation start
    time_t closingTime; // Tk - gates stop accepting

    uint32_t touristsInLowerStation; // After entry gate, before platform
    uint32_t touristsOnPlatform; // On chairs, in transit
    uint32_t touristsAtUpperStation; // Arrived at top, exiting via routes
    uint32_t totalRidesToday; // Cumulative ride count
    uint32_t cyclistsOnBikeTrailExit; // Cyclists currently exiting to bike trails
    uint32_t pedestriansOnWalkingExit; // Pedestrians currently exiting to walking path

    pid_t lowerWorkerPid; // Lower station controller
    pid_t upperWorkerPid; // Upper station controller

    uint64_t logSequenceNum; // Global log sequence counter for ordering

    time_t totalPausedSeconds; // Cumulative real seconds the simulation was suspended (Ctrl+Z)

    SharedOperationalState()
        : state{RopewayState::STOPPED},
          acceptingNewTourists{false},
          openingTime{0}, // Set by initializeState()
          closingTime{0}, // Set by initializeState()
          touristsInLowerStation{0},
          touristsOnPlatform{0},
          touristsAtUpperStation{0},
          totalRidesToday{0},
          cyclistsOnBikeTrailExit{0},
          pedestriansOnWalkingExit{0},
          lowerWorkerPid{0},
          upperWorkerPid{0},
          logSequenceNum{0},
          totalPausedSeconds{0} {
    }
};