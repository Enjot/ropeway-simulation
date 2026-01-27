#include "ipc/core/Semaphore.h"

#include <cerrno>
#include <cstdio>

#include "logging/Logger.h"

const char* Semaphore::Index::toString(const uint8_t index) {
    switch (index) {
        // Startup
        case CASHIER_READY: return "CASHIER_READY";
        case LOWER_WORKER_READY: return "LOWER_WORKER_READY";
        case UPPER_WORKER_READY: return "UPPER_WORKER_READY";
        // Tourist flow
        case CASHIER_QUEUE_SLOTS: return "CASHIER_QUEUE_SLOTS";
        case ENTRY_QUEUE_VIP_SLOTS: return "ENTRY_QUEUE_VIP_SLOTS";
        case ENTRY_QUEUE_REGULAR_SLOTS: return "ENTRY_QUEUE_REGULAR_SLOTS";
        case STATION_CAPACITY: return "STATION_CAPACITY";
        case BOARDING_QUEUE_WORK: return "BOARDING_QUEUE_WORK";
        case CHAIRS_AVAILABLE: return "CHAIRS_AVAILABLE";
        case CHAIR_ASSIGNED: return "CHAIR_ASSIGNED";
        case CURRENT_CHAIR_SLOTS: return "CURRENT_CHAIR_SLOTS";
        case EXIT_BIKE_TRAILS: return "EXIT_BIKE_TRAILS";
        case EXIT_WALKING_PATH: return "EXIT_WALKING_PATH";
        // Shared memory locks
        case SHM_OPERATIONAL: return "SHM_OPERATIONAL";
        case SHM_CHAIRS: return "SHM_CHAIRS";
        case SHM_STATS: return "SHM_STATS";
        // Logging
        case LOG_SEQUENCE: return "LOG_SEQUENCE";
        default: return "UNKNOWN_SEMAPHORE";
    }
}

Semaphore::Semaphore(const key_t key) {
    semId_ = semget(key, Index::TOTAL_SEMAPHORES, IPC_CREAT | IPC_EXCL | permissions);
    if (semId_ == -1) {
        if (errno == EEXIST) {
            semId_ = semget(key, Index::TOTAL_SEMAPHORES, permissions);
            if (semId_ == -1) {
                perror("semget (connect)");
                throw ipc_exception("Failed to connect to existing semaphore");
            }
            Logger::debug(tag_, "connected");
        } else {
            perror("semget (create)");
            throw ipc_exception("Failed to create semaphore");
        }
    } else {
        Logger::debug(tag_, "created");
    }
}

void Semaphore::initialize(const uint8_t semIndex, const int32_t value) const {
    semun arg{};
    arg.val = value;
    if (semctl(semId_, semIndex, SETVAL, arg) == -1) {
        perror("semctl SETVAL");
        throw ipc_exception("Failed to initialize semaphore");
    }
    Logger::debug(tag_, "initialized: %s with value: %d", Index::toString(semIndex), value);
}

bool Semaphore::wait(const uint8_t semIndex, const bool useUndo) const {
    sembuf operation{};
    operation.sem_num = semIndex;
    operation.sem_op = -1;
    operation.sem_flg = useUndo ? SEM_UNDO : 0;

    if (semop(semId_, &operation, 1) == -1) {
        if (errno == EINTR) return false;
        perror("semop wait");
        throw ipc_exception("Semaphore wait failed");
    }
    return true;
}

bool Semaphore::waitInterruptible(const uint8_t semIndex, const bool useUndo) const {
    sembuf operation{};
    operation.sem_num = semIndex;
    operation.sem_op = -1;
    operation.sem_flg = useUndo ? SEM_UNDO : 0;

    if (semop(semId_, &operation, 1) == -1) {
        if (errno == EINTR) return false;
        perror("semop waitInterruptible");
        throw ipc_exception("Semaphore waitInterruptible failed");
    }
    return true;
}

bool Semaphore::tryAcquire(const uint8_t semIndex, const bool useUndo) const {
    sembuf operation{};
    operation.sem_num = semIndex;
    operation.sem_op = -1;
    operation.sem_flg = IPC_NOWAIT | (useUndo ? SEM_UNDO : 0);

    if (semop(semId_, &operation, 1) == -1) {
        if (errno == EAGAIN) return false;
        if (errno == EINTR) return false;
        perror("semop tryAcquire");
        throw ipc_exception("Semaphore tryAcquire failed");
    }
    return true;
}

bool Semaphore::tryAcquire(const uint8_t semIndex, const int32_t n, const bool useUndo) const {
    if (n <= 0) return true;

    sembuf operation{};
    operation.sem_num = semIndex;
    operation.sem_op = static_cast<short>(-n);
    operation.sem_flg = IPC_NOWAIT | (useUndo ? SEM_UNDO : 0);

    if (semop(semId_, &operation, 1) == -1) {
        if (errno == EAGAIN) return false;
        if (errno == EINTR) return false;
        perror("semop tryAcquire");
        throw ipc_exception("Semaphore tryAcquire failed");
    }
    return true;
}

bool Semaphore::wait(const uint8_t semIndex, const int32_t n, const bool useUndo) const {
    if (n <= 0) return true;

    sembuf operation{};
    operation.sem_num = semIndex;
    operation.sem_op = static_cast<short>(-n);
    operation.sem_flg = useUndo ? SEM_UNDO : 0;

    if (semop(semId_, &operation, 1) == -1) {
        if (errno == EINTR) return false;
        perror("semop wait");
        throw ipc_exception("Semaphore wait failed");
    }
    return true;
}

void Semaphore::post(const uint8_t semIndex, const bool useUndo) const {
    sembuf operation{};
    operation.sem_num = semIndex;
    operation.sem_op = 1;
    operation.sem_flg = useUndo ? SEM_UNDO : 0;

    while (semop(semId_, &operation, 1) == -1) {
        if (errno == EINTR) continue;
        perror("semop post");
        throw ipc_exception("Semaphore post failed");
    }
}

void Semaphore::post(const uint8_t semIndex, const int32_t n, const bool useUndo) const {
    if (n <= 0) return;

    sembuf operation{};
    operation.sem_num = semIndex;
    operation.sem_op = static_cast<short>(n);
    operation.sem_flg = useUndo ? SEM_UNDO : 0;

    while (semop(semId_, &operation, 1) == -1) {
        if (errno == EINTR) continue;
        perror("semop post");
        throw ipc_exception("Semaphore post failed");
    }
}

void Semaphore::setValue(const uint8_t semIndex, const int32_t value) const {
    semun arg{};
    arg.val = value;
    if (semctl(semId_, semIndex, SETVAL, arg) == -1) {
        perror("semctl SETVAL");
        throw ipc_exception("Failed to set semaphore value");
    }
}

int32_t Semaphore::getAvailableSpace(const uint8_t semIndex) const {
    const int32_t val = semctl(semId_, semIndex, GETVAL);
    if (val == -1) {
        perror("semctl GETVAL");
        throw ipc_exception("Failed to get semaphore value");
    }
    return val;
}

void Semaphore::destroy() const {
    if (semctl(semId_, 0, IPC_RMID) == -1) {
        perror("semctl IPC_RMID");
        throw ipc_exception("Failed to destroy semaphore");
    }
    Logger::debug(tag_, "destroyed");
}

Semaphore::ScopedLock::ScopedLock(const Semaphore& sem, const uint8_t semIndex)
    : sem_(sem), semIndex_(semIndex) {
    sem_.wait(semIndex_);
}

Semaphore::ScopedLock::~ScopedLock() {
    sem_.post(semIndex_);
}
