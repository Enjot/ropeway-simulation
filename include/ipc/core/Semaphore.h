#pragma once

#include <cstdint>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include "IpcException.h"

#ifdef _SEM_SEMUN_UNDEFINED
/**
 * @brief Union for semaphore control operations.
 *
 * Required on some systems where semun is not defined.
 */
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};
#endif

/**
 * @class Semaphore
 * @brief RAII wrapper for System V semaphore sets.
 *
 * Provides a safe C++ interface to System V semaphores for inter-process
 * synchronization. Creates a semaphore set with TOTAL_SEMAPHORES members.
 *
 * @note All operations are signal-safe (handle EINTR).
 * @note Uses SEM_UNDO for automatic cleanup on process termination.
 */
class Semaphore {
public:
    /**
     * @brief Semaphore indices for the ropeway simulation.
     *
     * Defines all semaphores used in the simulation, organized by purpose.
     */
    struct Index {
        enum : uint8_t {
            // === STARTUP ===
            LOGGER_READY = 0, // Logger signals readiness to main process
            CASHIER_READY, // Cashier signals readiness to main process
            LOWER_WORKER_READY, // Lower station worker signals readiness
            UPPER_WORKER_READY, // Upper station worker signals readiness

            // === TOURIST FLOW (chronological order) ===

            // 1. Buy ticket at cashier
            CASHIER_QUEUE_SLOTS, // Flow control for cashier request queue

            // 2. Request entry to station
            ENTRY_QUEUE_VIP_SLOTS, // Reserved entry queue slots for VIPs
            ENTRY_QUEUE_REGULAR_SLOTS, // Entry queue slots for regular tourists

            // 3. Enter lower station
            STATION_CAPACITY, // Max tourists allowed on lower station (N)

            // 4. Wait for boarding
            BOARDING_QUEUE_WORK, // Signals LowerWorker to process queues

            // 5. Board chair
            CHAIRS_AVAILABLE, // Available chairs for dispatch (max 36 concurrent)
            CHAIR_ASSIGNED, // Signals tourist that chair has been assigned
            CURRENT_CHAIR_SLOTS, // Available slots on current boarding chair (0-4)

            // 6. Exit at upper station
            EXIT_BIKE_TRAILS, // Capacity for cyclists exiting to downhill trails
            EXIT_WALKING_PATH, // Capacity for pedestrians exiting to walking paths

            // === SHARED MEMORY LOCKS ===
            // Lock ordering: SHM_OPERATIONAL -> SHM_CHAIRS -> SHM_STATS
            SHM_OPERATIONAL, // Protects operational state (ropeway state, counters, PIDs)
            SHM_CHAIRS, // Protects chair pool and boarding queue
            SHM_STATS, // Protects statistics and gate passage log

            // === LOGGING ===
            LOG_SEQUENCE, // Protects log sequence number increment
            LOG_QUEUE_SLOTS, // Available slots in log queue (prevents overflow)

            TOTAL_SEMAPHORES
        };

        /**
         * @brief Get human-readable name of a semaphore index.
         * @param index Semaphore index value
         * @return String name of the semaphore
         */
        static const char *toString(uint8_t index);
    };

    /**
     * @brief Construct semaphore set wrapper.
     * @param key System V IPC key for the semaphore set
     * @throws ipc_exception If semget fails
     */
    explicit Semaphore(key_t key);

    ~Semaphore() = default;

    Semaphore(const Semaphore &) = delete;

    Semaphore &operator=(const Semaphore &) = delete;

    /**
     * @brief Initialize a semaphore to a specific value.
     * @param semIndex Index of the semaphore in the set
     * @param value Initial value to set
     */
    void initialize(uint8_t semIndex, int32_t value) const;

    /**
     * @brief Wait (decrement) on a semaphore.
     * @param semIndex Index of the semaphore in the set
     * @param n Amount to decrement (typically 1)
     * @param useUndo If true, uses SEM_UNDO for automatic cleanup on process termination
     * @return true if successful, false if interrupted by signal
     *
     * Blocks until the semaphore value is >= n, then decrements by n.
     * Handles EINTR for signal safety.
     */
    bool wait(uint8_t semIndex, int32_t n, bool useUndo) const;

    /**
     * @brief Try to acquire a semaphore without blocking.
     * @param semIndex Index of the semaphore in the set
     * @param n Amount to decrement (typically 1)
     * @param useUndo If true, uses SEM_UNDO for automatic cleanup
     * @return true if acquired, false if would block
     */
    bool tryAcquire(uint8_t semIndex, int32_t n, bool useUndo) const;

    /**
     * @brief Post (increment) a semaphore.
     * @param semIndex Index of the semaphore in the set
     * @param n Amount to increment (typically 1)
     * @param useUndo If true, uses SEM_UNDO for automatic cleanup
     */
    void post(uint8_t semIndex, int32_t n, bool useUndo) const;

    /**
     * @brief Set a semaphore to an absolute value.
     * @param semIndex Index of the semaphore in the set
     * @param value Value to set
     */
    void setValue(uint8_t semIndex, int32_t value) const;

    /**
     * @brief Get current value of a semaphore.
     * @param semIndex Index of the semaphore in the set
     * @return Current semaphore value
     */
    [[nodiscard]] int32_t getAvailableSpace(uint8_t semIndex) const;

    /**
     * @brief Destroy the semaphore set.
     * @throws ipc_exception If semctl IPC_RMID fails
     */
    void destroy() const;

    /**
     * @brief RAII lock guard for semaphores.
     *
     * Acquires the semaphore on construction and releases on destruction.
     * Uses SEM_UNDO for safety.
     */
    class ScopedLock {
    public:
        /**
         * @brief Acquire the semaphore.
         * @param sem Reference to the Semaphore wrapper
         * @param semIndex Index of the semaphore to lock
         */
        explicit ScopedLock(const Semaphore &sem, uint8_t semIndex);

        /** @brief Release the semaphore. */
        ~ScopedLock();

        ScopedLock(const ScopedLock &) = delete;

        ScopedLock &operator=(const ScopedLock &) = delete;

    private:
        const Semaphore &sem_;
        uint8_t semIndex_;
        bool acquired_{false};  ///< Whether lock was successfully acquired
    };

private:
    static constexpr auto tag_{"Semaphore"};
    int32_t semId_;
    static constexpr int32_t permissions = 0600;
};