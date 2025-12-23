#pragma once
#include <cstdint>

/**
 * Semaphore indices for semaphore arrays
 * Use these constants to access specific semaphores in the semaphore set
 */
namespace SemaphoreIndex {
    constexpr uint8_t ENTRY_GATES{0}; // Controls entry gate access
    constexpr uint8_t RIDE_GATES{1}; // Controls ride gate access
    constexpr uint8_t STATION_CAPACITY{2}; // Limits tourists in the station area
    constexpr uint8_t CHAIR_ALLOCATION{3}; // Controls chair allocation
    constexpr uint8_t SHARED_MEMORY{4}; // Protects shared memory access
    constexpr uint8_t WORKER_SYNC{5}; // Worker synchronization
    constexpr uint8_t TOTAL_SEMAPHORES{6}; // Total number of semaphores needed
}
