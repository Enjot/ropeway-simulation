#pragma once

#include "core/Config.hpp"
#include "ropeway/chair/Chair.hpp"
#include "ropeway/chair/BoardingQueue.hpp"

/**
 * Chair management pool.
 * Contains all chairs and the boarding queue.
 *
 * OWNERSHIP: LowerWorker manages assignments; tourists update on board/disembark.
 */
struct SharedChairPoolState {
    Chair chairs[Config::Chair::QUANTITY];  // All 72 chairs
    uint32_t chairsInUse;                   // Currently occupied chairs
    BoardingQueue boardingQueue;            // Tourists waiting to board

    SharedChairPoolState()
        : chairs{},
          chairsInUse{0},
          boardingQueue{} {}
};
