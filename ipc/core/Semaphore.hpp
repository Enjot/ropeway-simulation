#pragma once
#include <cerrno>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include "IpcException.hpp"
#include "utils/Logger.hpp"

#ifdef _SEM_SEMUN_UNDEFINED
/**
 * @brief Union used for semaphore control operations.
 * This is required on Linux systems for certain `semctl` operations.
 */
union semun {
    int val; /* Value for SETVAL */
    struct semid_ds *buf; /* Buffer for IPC_STAT, IPC_SET */
    unsigned short *array; /* Array for GETALL, SETALL */
    struct seminfo *__buf; /* Buffer for IPC_INFO (Linux-specific) */
};
#endif

/**
 * @class Semaphore
 * @brief A wrapper class for System V semaphores.
 * Provides functionality to create, initialize, and manage semaphores.
 */
class Semaphore {
public:
    /**
     * @struct Index
     * @brief Enum-like structure to define indices for semaphores.
     */
    struct Index {
        enum : uint8_t {
            ENTRY_GATES = 0,
            RIDE_GATES,
            STATION_CAPACITY,
            CHAIR_ALLOCATION,
            SHARED_MEMORY,
            WORKER_SYNC,
            CASHIER_READY,
            LOWER_WORKER_READY,
            UPPER_WORKER_READY,
            CHAIR_ASSIGNED,
            BOARDING_QUEUE_WORK,
            TOTAL_SEMAPHORES
        };

        static auto toString(const uint8_t index) {
            switch (index) {
                case ENTRY_GATES: return "ENTRY_GATES";
                case RIDE_GATES: return "RIDE_GATES";
                case STATION_CAPACITY: return "STATION_CAPACITY";
                case CHAIR_ALLOCATION: return "CHAIR_ALLOCATION";
                case SHARED_MEMORY: return "SHARED_MEMORY";
                case WORKER_SYNC: return "WORKER_SYNC";
                case CASHIER_READY: return "CASHIER_READY";
                case LOWER_WORKER_READY: return "LOWER_WORKER_READY";
                case UPPER_WORKER_READY: return "UPPER_WORKER_READY";
                case CHAIR_ASSIGNED: return "CHAIR_ASSIGNED";
                case BOARDING_QUEUE_WORK: return "BOARDING_QUEUE_WORK";
                default: return "UNKNOWN_SEMAPHORE";
            }
        }
    };

    /**
     * @brief Constructs a Semaphore object.
     * Creates a new semaphore set or connects to an existing one.
     * @param key The key used to identify the semaphore set.
     * @throws ipc_exception If the semaphore set cannot be created or connected.
     */
    explicit Semaphore(const key_t key) {
        semId_ = semget(key, Index::TOTAL_SEMAPHORES, IPC_CREAT | IPC_EXCL | permissions);
        if (semId_ == -1) {
            if (errno == EEXIST) {
                semId_ = semget(key, Index::TOTAL_SEMAPHORES, permissions);
                if (semId_ == -1) {
                    throw ipc_exception("Failed to connect to existing semaphore");
                }
                Logger::debug(tag_, "connected");
            } else {
                throw ipc_exception("Failed to create semaphore");
            }
        } else {
            Logger::debug(tag_, "created");
        }
    }

    Semaphore(const Semaphore &) = delete;

    Semaphore &operator=(const Semaphore &) = delete;

    ~Semaphore() = default;

    /**
     * @brief Initializes specific semaphore in the set.
     * @param semIndex The index of the semaphore to initialize.
     * @param value The value to set for the semaphore.
     * @throws ipc_exception If the initialization fails.
     */
    void initialize(const uint8_t semIndex, const int32_t value) const {
        semun arg{};
        arg.val = value;
        if (semctl(semId_, semIndex, SETVAL, arg) == -1) {
            throw ipc_exception("Failed to initialize semaphore");
        }
        Logger::debug(tag_, "initialized: %s with value: %d", Index::toString(semIndex), value);
    }

    /**
     * @brief Waits (decrements) on a semaphore.
     * Blocks until the semaphore value is greater than zero.
     * @param semIndex The index of the semaphore to wait on.
     * @param useUndo Whether to use SEM_UNDO for automatic cleanup.
     * @throws ipc_exception If the wait operation fails.
     */
    void wait(const uint8_t semIndex, const bool useUndo = true) const {
        sembuf operation{};
        operation.sem_num = semIndex;
        operation.sem_op = -1;
        operation.sem_flg = useUndo ? SEM_UNDO : 0;

        while (semop(semId_, &operation, 1) == -1) {
            if (errno == EINTR) continue;
            throw ipc_exception("Semaphore wait failed");
        }
    }

    /**
     * @brief Posts (increments) semaphore.
     * Increments the semaphore value, potentially unblocking other processes.
     * @param semIndex The index of the semaphore to post to.
     * @param useUndo Whether to use SEM_UNDO for automatic cleanup.
     * @throws ipc_exception If the post-operation fails.
     */
    void post(const uint8_t semIndex, const bool useUndo = true) const {
        sembuf operation{};
        operation.sem_num = semIndex;
        operation.sem_op = 1;
        operation.sem_flg = useUndo ? SEM_UNDO : 0;

        while (semop(semId_, &operation, 1) == -1) {
            if (errno == EINTR) continue;
            throw ipc_exception("Semaphore post failed");
        }
    }

    /**
    * @brief Gets the current value of semaphore.
    * @param semIndex The index of the semaphore.
    * @return The current semaphore value.
    * @throws ipc_exception If getting the value fails.
    */
    [[nodiscard]] int32_t getAvailableSpace(const uint8_t semIndex) const {
        const int32_t val = semctl(semId_, semIndex, GETVAL);
        if (val == -1) {
            throw ipc_exception("Failed to get semaphore value");
        }
        return val;
    }

    /**
     * @brief Removes a semaphore set identified by a key.
     */
    void destroy() const {
        if (semctl(semId_, 0, IPC_RMID) == -1) {
            throw ipc_exception("Failed to destroy semaphore");
        }
        Logger::debug(tag_, "destroyed");
    }

    /**
     * @class ScopedLock
     * @brief A RAII-style lock for semaphore.
     * Acquires the semaphore on construction and releases it on destruction.
     */
    class ScopedLock {
    public:
        /**
         * @brief Constructs a ScopedLock and acquires the semaphore.
         * @param sem The Semaphore object to lock.
         * @param semIndex The index of the semaphore to lock.
         */
        explicit ScopedLock(const Semaphore &sem, const uint8_t semIndex)
            : sem_(sem), semIndex_(semIndex) {
            sem_.wait(semIndex_);
        }

        ScopedLock(const ScopedLock &) = delete;

        ScopedLock &operator=(const ScopedLock &) = delete;

        /**
         * @brief Destructor that releases the semaphore.
         */
        ~ScopedLock() {
            sem_.post(semIndex_);
        }

    private:
        const Semaphore &sem_;
        uint8_t semIndex_;
    };

private:
    static constexpr auto tag_{"Semaphore"};
    int32_t semId_;
    /**
     * @brief The default permissions used for creating or accessing semaphore sets.
     * Specifies the access permissions for the semaphore set as a bitmask.
     */
    static constexpr int32_t permissions = 0600;

    /**
     * @brief Initializes all semaphores in the set to a locked state (value = 0).
     */
    void initializeAllToLockedState() const {
        for (int i = 0; i < Index::TOTAL_SEMAPHORES; ++i) {
            initialize(i, 0);
        }
    }
};
