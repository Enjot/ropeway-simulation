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
            ENTRY_GATES = 0,
            RIDE_GATES,
            STATION_CAPACITY,
            CHAIR_ALLOCATION,
            SHM_OPERATIONAL,
            SHM_CHAIRS,
            SHM_STATS,
            WORKER_SYNC,
            CASHIER_READY,
            LOWER_WORKER_READY,
            UPPER_WORKER_READY,
            CHAIR_ASSIGNED,
            BOARDING_QUEUE_WORK,
            ENTRY_QUEUE_WORK,
            EXIT_BIKE_TRAILS, // Upper station exit to downhill bike trails (cyclists)
            EXIT_WALKING_PATH, // Upper station exit to walking paths (pedestrians)
            TOTAL_SEMAPHORES
        };

        static const char* toString(uint8_t index);
    };

    explicit Semaphore(key_t key);
    ~Semaphore() = default;

    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    void initialize(uint8_t semIndex, int32_t value) const;
    bool wait(uint8_t semIndex, bool useUndo = true) const;
    bool waitInterruptible(uint8_t semIndex, bool useUndo = true) const;
    bool tryAcquire(uint8_t semIndex, bool useUndo = true) const;
    void post(uint8_t semIndex, bool useUndo = true) const;
    [[nodiscard]] int32_t getAvailableSpace(uint8_t semIndex) const;
    void destroy() const;

    class ScopedLock {
    public:
        explicit ScopedLock(const Semaphore& sem, uint8_t semIndex);
        ~ScopedLock();
        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;

    private:
        const Semaphore& sem_;
        uint8_t semIndex_;
    };

private:
    static constexpr auto tag_{"Semaphore"};
    int32_t semId_;
    static constexpr int32_t permissions = 0600;
};
