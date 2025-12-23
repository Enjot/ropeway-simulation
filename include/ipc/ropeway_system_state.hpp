#pragma once

#include <cstdint>
#include <sys/types.h>
#include "common/ropeway_state.hpp"
#include "common/config.hpp"
#include "structures/chair.hpp"

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
                           openingTime{Config::Ropeway::DEFAULT_OPENING_TIME},
                           closingTime{Config::Ropeway::DEFAULT_CLOSING_TIME},
                           acceptingNewTourists{false},
                           totalRidesToday{0},
                           worker1Pid{0},
                           worker2Pid{0} {
    }
};
