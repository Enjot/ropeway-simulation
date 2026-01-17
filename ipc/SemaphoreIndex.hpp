#pragma once
#include <cstdint>

/**
 * Semaphore indices for semaphore arrays
 * Use these constants to access specific semaphores in the semaphore set
 */
namespace SemaphoreIndex {
    constexpr uint8_t ENTRY_GATES{0};       // Controls entry gate access
    constexpr uint8_t RIDE_GATES{1};        // Controls ride gate access
    constexpr uint8_t STATION_CAPACITY{2};  // Limits tourists in the station area
    constexpr uint8_t CHAIR_ALLOCATION{3};  // Controls chair allocation
    constexpr uint8_t SHARED_MEMORY{4};     // Protects shared memory access
    constexpr uint8_t WORKER_SYNC{5};       // Worker synchronization

    // Process readiness semaphores (for startup synchronization)
    constexpr uint8_t CASHIER_READY{6};     // Signaled when cashier is ready
    constexpr uint8_t WORKER1_READY{7};     // Signaled when worker1 is ready
    constexpr uint8_t WORKER2_READY{8};     // Signaled when worker2 is ready

    // Event notification semaphores (for blocking instead of polling)
    constexpr uint8_t CHAIR_ASSIGNED{9};       // Signaled when a chair is assigned to tourists
    constexpr uint8_t BOARDING_QUEUE_WORK{10}; // Signaled when tourist joins boarding queue

    constexpr uint8_t TOTAL_SEMAPHORES{11}; // Total number of semaphores needed
}
