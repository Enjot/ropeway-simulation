#pragma once

/**
 * Semaphore indices for semaphore arrays
 * Use these constants to access specific semaphores in the semaphore set
 */
namespace SemaphoreIndex {
    constexpr int ENTRY_GATES = 0;           // Controls entry gate access
    constexpr int RIDE_GATES = 1;            // Controls ride gate access
    constexpr int STATION_CAPACITY = 2;      // Limits tourists in the station area
    constexpr int CHAIR_ALLOCATION = 3;      // Controls chair allocation
    constexpr int SHARED_MEMORY = 4;         // Protects shared memory access
    constexpr int WORKER_SYNC = 5;           // Worker synchronization
    constexpr int TOTAL_SEMAPHORES = 6;      // Total number of semaphores needed
}
