#pragma once

#include "core/Config.h"
#include "ropeway/chair/Chair.h"
#include "ropeway/chair/BoardingQueue.h"

/**
 * Chair management pool.
 * Contains all chairs and the boarding queue.
 *
 * OWNERSHIP: LowerWorker manages assignments; tourists update on board/disembark.
 */
struct SharedChairPoolState {
    Chair chairs[Constants::Chair::QUANTITY]; // All 72 chairs
    uint32_t chairsInUse; // Currently occupied chairs
    BoardingQueue boardingQueue; // Tourists waiting to board

    SharedChairPoolState()
        : chairs{},
          chairsInUse{0},
          boardingQueue{} {
    }
};
