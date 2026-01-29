#pragma once

#include <cstdint>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include "IpcException.h"

#ifdef _SEM_SEMUN_UNDEFINED
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};
#endif

/**
 * @class Semaphore
 * @brief A wrapper class for System V semaphores.
 */
class Semaphore {
public:
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

        static const char *toString(uint8_t index);
    };

    explicit Semaphore(key_t key);

    ~Semaphore() = default;

    Semaphore(const Semaphore &) = delete;

    Semaphore &operator=(const Semaphore &) = delete;

    void initialize(uint8_t semIndex, int32_t value) const;

    bool wait(uint8_t semIndex, int32_t n, bool useUndo) const;

    bool tryAcquire(uint8_t semIndex, int32_t n, bool useUndo) const;

    void post(uint8_t semIndex, int32_t n, bool useUndo) const;

    void setValue(uint8_t semIndex, int32_t value) const;

    [[nodiscard]] int32_t getAvailableSpace(uint8_t semIndex) const;

    void destroy() const;

    class ScopedLock {
    public:
        explicit ScopedLock(const Semaphore &sem, uint8_t semIndex);

        ~ScopedLock();

        ScopedLock(const ScopedLock &) = delete;

        ScopedLock &operator=(const ScopedLock &) = delete;

    private:
        const Semaphore &sem_;
        uint8_t semIndex_;
    };

private:
    static constexpr auto tag_{"Semaphore"};
    int32_t semId_;
    static constexpr int32_t permissions = 0600;
};