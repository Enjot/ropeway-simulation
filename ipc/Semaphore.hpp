#pragma once

#include <sys/ipc.h>
#include <sys/sem.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <cstdint>

/**
 * Union for semctl operations
 * Required by System V semaphore API
 * Note: macOS/BSD already defines this in sys/sem.h
 */
#if defined(__linux__) && !defined(_SEM_SEMUN_UNDEFINED)
union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};
#endif

/**
 * RAII wrapper for System V semaphore set
 * Supports multiple semaphores in a single set
 */
class Semaphore {
public:
    /**
     * Create or attach to semaphore set
     * @param key IPC key for semaphore set
     * @param numSemaphores Number of semaphores in the set
     * @param create If true, creates new set; if false, attaches to existing
     */
    explicit Semaphore(key_t key, uint32_t numSemaphores, bool create = true)
        : key_{key}, semId_{-1}, numSemaphores_{numSemaphores}, isOwner_{create} {

        if (create) {
            semId_ = semget(key_, static_cast<int>(numSemaphores_), IPC_CREAT | IPC_EXCL | 0600);
            if (semId_ == -1) {
                perror("semget (create)");
                throw std::runtime_error("Failed to create semaphore set: " +
                    std::string(strerror(errno)));
            }
        } else {
            semId_ = semget(key_, static_cast<int>(numSemaphores_), 0600);
            if (semId_ == -1) {
                perror("semget (attach)");
                throw std::runtime_error("Failed to get semaphore set: " +
                    std::string(strerror(errno)));
            }
        }
    }

    ~Semaphore() {
        if (isOwner_ && semId_ != -1) {
            if (semctl(semId_, 0, IPC_RMID) == -1) {
                perror("semctl IPC_RMID");
            }
        }
    }

    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    Semaphore(Semaphore&& other) noexcept
        : key_{other.key_}, semId_{other.semId_},
          numSemaphores_{other.numSemaphores_}, isOwner_{other.isOwner_} {
        other.semId_ = -1;
        other.isOwner_ = false;
    }

    Semaphore& operator=(Semaphore&& other) noexcept {
        if (this != &other) {
            if (isOwner_ && semId_ != -1) {
                semctl(semId_, 0, IPC_RMID);
            }

            key_ = other.key_;
            semId_ = other.semId_;
            numSemaphores_ = other.numSemaphores_;
            isOwner_ = other.isOwner_;

            other.semId_ = -1;
            other.isOwner_ = false;
        }
        return *this;
    }

    /**
     * Initialize a semaphore to a specific value
     * @param semNum Semaphore index in the set
     * @param value Initial value
     */
    bool setValue(uint32_t semNum, int value) {
        if (semNum >= numSemaphores_) {
            return false;
        }
        semun arg{};
        arg.val = value;
        if (semctl(semId_, static_cast<int>(semNum), SETVAL, arg) == -1) {
            perror("semctl SETVAL");
            return false;
        }
        return true;
    }

    /**
     * Get current value of a semaphore
     */
    [[nodiscard]] int getValue(uint32_t semNum) const {
        if (semNum >= numSemaphores_) {
            return -1;
        }
        int val = semctl(semId_, static_cast<int>(semNum), GETVAL);
        if (val == -1) {
            perror("semctl GETVAL");
        }
        return val;
    }

    /**
     * Wait (P operation) - decrements semaphore, blocks if zero
     * @param semNum Semaphore index
     * @return true on success, false on error
     */
    bool wait(uint32_t semNum) {
        return operate(semNum, -1, 0);
    }

    /**
     * Wait with decrement by specific amount
     */
    bool wait(uint32_t semNum, int amount) {
        return operate(semNum, -amount, 0);
    }

    /**
     * Signal (V operation) - increments semaphore
     * @param semNum Semaphore index
     * @return true on success, false on error
     */
    bool signal(uint32_t semNum) {
        return operate(semNum, 1, 0);
    }

    /**
     * Signal with increment by specific amount
     */
    bool signal(uint32_t semNum, int amount) {
        return operate(semNum, amount, 0);
    }

    /**
     * Try wait (non-blocking P operation)
     * @param semNum Semaphore index
     * @return true if acquired, false if would block or error
     */
    bool tryWait(uint32_t semNum) {
        return operate(semNum, -1, IPC_NOWAIT);
    }

    /**
     * Wait for semaphore to become zero
     */
    bool waitZero(uint32_t semNum) {
        return operate(semNum, 0, 0);
    }

    [[nodiscard]] int getId() const noexcept { return semId_; }
    [[nodiscard]] key_t getKey() const noexcept { return key_; }
    [[nodiscard]] uint32_t getNumSemaphores() const noexcept { return numSemaphores_; }
    [[nodiscard]] bool isOwner() const noexcept { return isOwner_; }

    /**
     * Release ownership (segment won't be removed on destruction)
     */
    void releaseOwnership() noexcept {
        isOwner_ = false;
    }

    /**
     * Remove the semaphore set
     */
    bool remove() {
        if (semId_ != -1) {
            if (semctl(semId_, 0, IPC_RMID) == -1) {
                perror("semctl IPC_RMID (remove)");
                return false;
            }
            isOwner_ = false;
            return true;
        }
        return false;
    }

    /**
     * Check if semaphore set exists for given key
     */
    static bool exists(key_t key) {
        int id = semget(key, 0, 0);
        return id != -1;
    }

    /**
     * Remove existing semaphore set by key (cleanup utility)
     */
    static bool removeByKey(key_t key) {
        int id = semget(key, 0, 0);
        if (id == -1) {
            return false;
        }
        if (semctl(id, 0, IPC_RMID) == -1) {
            perror("semctl IPC_RMID (removeByKey)");
            return false;
        }
        return true;
    }

private:
    /**
     * Perform semaphore operation
     */
    bool operate(uint32_t semNum, short op, short flags) {
        if (semNum >= numSemaphores_) {
            return false;
        }

        struct sembuf operation{};
        operation.sem_num = static_cast<unsigned short>(semNum);
        operation.sem_op = op;
        operation.sem_flg = static_cast<short>(flags);

        while (true) {
            if (semop(semId_, &operation, 1) == -1) {
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN && (flags & IPC_NOWAIT)) {
                    return false;
                }
                // EIDRM: semaphore was removed during shutdown - exit gracefully
                if (errno == EIDRM) {
                    return false;
                }
                perror("semop");
                return false;
            }
            return true;
        }
    }

    key_t key_;
    int semId_;
    uint32_t numSemaphores_;
    bool isOwner_;
};

/**
 * RAII lock guard for semaphore
 * Automatically waits on construction and signals on destruction
 */
class SemaphoreLock {
public:
    SemaphoreLock(Semaphore& sem, uint32_t semNum)
        : sem_{sem}, semNum_{semNum}, locked_{false} {
        if (sem_.wait(semNum_)) {
            locked_ = true;
        }
    }

    ~SemaphoreLock() {
        if (locked_) {
            sem_.signal(semNum_);
        }
    }

    SemaphoreLock(const SemaphoreLock&) = delete;
    SemaphoreLock& operator=(const SemaphoreLock&) = delete;

    [[nodiscard]] bool isLocked() const noexcept { return locked_; }

    void unlock() {
        if (locked_) {
            sem_.signal(semNum_);
            locked_ = false;
        }
    }

private:
    Semaphore& sem_;
    uint32_t semNum_;
    bool locked_;
};
