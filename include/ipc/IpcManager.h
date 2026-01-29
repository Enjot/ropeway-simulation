#pragma once

#include <cstdlib>

#include "ipc/core/SharedMemory.h"
#include "ipc/core/Semaphore.h"
#include "ipc/core/MessageQueue.h"
#include "ipc/core/IpcException.h"
#include "ipc/model/SharedRopewayState.h"
#include "ropeway/worker/WorkerMessage.h"
#include "entrance/CashierMessage.h"
#include "ropeway/gate/EntryGateMessage.h"
#include "logging/LogMessage.h"
#include "core/Config.h"
#include "logging/Logger.h"

class IpcManager;

/**
 * @brief Cleanup handler namespace for IpcManager.
 *
 * Registers an atexit handler to ensure IPC resources are cleaned up
 * even on abnormal termination.
 */
namespace IpcCleanup {
    inline IpcManager *g_instance = nullptr;

    /**
     * @brief atexit handler for IPC cleanup.
     *
     * Called automatically at program exit to release IPC resources.
     */
    void atexitHandler();
}

/**
 * @brief Central manager for all IPC resources.
 *
 * Creates and manages shared memory, semaphores, and message queues
 * used by the simulation. Provides RAII cleanup of all resources.
 *
 * Only the main orchestrator process should create an IpcManager.
 * Child processes should attach to resources using individual wrappers.
 */
class IpcManager {
public:
    /**
     * @brief Create all IPC resources for the simulation.
     * @throws ipc_exception If any IPC creation fails
     *
     * Creates shared memory, semaphore set, and all message queues.
     * Registers cleanup handler for automatic resource release.
     */
    IpcManager()
        : shmKey_{ftok(".", 'S')},
          semKey_{ftok(".", 'M')},
          workerMsgKey_{ftok(".", 'W')},
          cashierMsgKey_{ftok(".", 'C')},
          entryGateMsgKey_{ftok(".", 'E')},
          logMsgKey_{ftok(".", 'L')},
          shm_{SharedMemory<SharedRopewayState>::create(shmKey_)},
          sem_{semKey_},
          workerQueue_{workerMsgKey_, "WorkerMessageQueue"},
          cashierQueue_{cashierMsgKey_, "CashierMessageQueue"},
          entryGateQueue_{entryGateMsgKey_, "EntryGateQueue"},
          logQueue_{logMsgKey_, "LogMessageQueue"} {
        if (shmKey_ == -1 || semKey_ == -1 || workerMsgKey_ == -1 || cashierMsgKey_ == -1 || entryGateMsgKey_ == -1 ||
            logMsgKey_ == -1) {
            throw ipc_exception("ftok failed");
        }

        IpcCleanup::g_instance = this;
        std::atexit(IpcCleanup::atexitHandler);

        Logger::debug(Logger::Source::Other, tag_, "created");
    }

    ~IpcManager() {
        cleanup();
        IpcCleanup::g_instance = nullptr;
    }

    IpcManager(const IpcManager &) = delete;

    IpcManager &operator=(const IpcManager &) = delete;

    IpcManager(IpcManager &&) = delete;

    IpcManager &operator=(IpcManager &&) = delete;

    /** @brief Get pointer to shared ropeway state. */
    SharedRopewayState *state() { return shm_.get(); }
    /** @brief Access shared state via pointer operator. */
    SharedRopewayState *operator->() { return shm_.get(); }

    /** @brief Get reference to semaphore set wrapper. */
    Semaphore &sem() { return sem_; }
    /** @brief Get reference to worker message queue. */
    MessageQueue<WorkerMessage> &workerQueue() { return workerQueue_; }
    /** @brief Get reference to cashier message queue. */
    MessageQueue<TicketRequest> &cashierQueue() { return cashierQueue_; }
    /** @brief Get reference to entry gate message queue. */
    MessageQueue<EntryGateRequest> &entryGateQueue() { return entryGateQueue_; }
    /** @brief Get reference to log message queue. */
    MessageQueue<LogMessage> &logQueue() { return logQueue_; }

    /** @brief Get shared memory key for child processes. */
    key_t shmKey() const { return shmKey_; }
    /** @brief Get semaphore set key for child processes. */
    key_t semKey() const { return semKey_; }
    /** @brief Get worker message queue key for child processes. */
    key_t workerMsgKey() const { return workerMsgKey_; }
    /** @brief Get cashier message queue key for child processes. */
    key_t cashierMsgKey() const { return cashierMsgKey_; }
    /** @brief Get entry gate message queue key for child processes. */
    key_t entryGateMsgKey() const { return entryGateMsgKey_; }
    /** @brief Get log message queue key for child processes. */
    key_t logMsgKey() const { return logMsgKey_; }

    /**
     * @brief Initialize all semaphores to their starting values.
     * @param stationCapacity Maximum tourists allowed in lower station
     *
     * Must be called after construction and before starting simulation.
     */
    void initSemaphores(const uint16_t stationCapacity) const {
        // Startup synchronization
        sem_.initialize(Semaphore::Index::LOGGER_READY, 0);
        sem_.initialize(Semaphore::Index::CASHIER_READY, 0);
        sem_.initialize(Semaphore::Index::LOWER_WORKER_READY, 0);
        sem_.initialize(Semaphore::Index::UPPER_WORKER_READY, 0);

        // Tourist flow (chronological order)
        sem_.initialize(Semaphore::Index::CASHIER_QUEUE_SLOTS, Constants::Queue::CASHIER_QUEUE_CAPACITY);
        sem_.initialize(Semaphore::Index::ENTRY_QUEUE_VIP_SLOTS, Constants::Queue::ENTRY_QUEUE_VIP_SLOTS);
        sem_.initialize(Semaphore::Index::ENTRY_QUEUE_REGULAR_SLOTS, Constants::Queue::ENTRY_QUEUE_REGULAR_SLOTS);
        sem_.initialize(Semaphore::Index::STATION_CAPACITY, stationCapacity);
        sem_.initialize(Semaphore::Index::BOARDING_QUEUE_WORK, 0);
        sem_.initialize(Semaphore::Index::CHAIRS_AVAILABLE, Constants::Chair::MAX_CONCURRENT_IN_USE);
        sem_.initialize(Semaphore::Index::CHAIR_ASSIGNED, 0);
        sem_.initialize(Semaphore::Index::CURRENT_CHAIR_SLOTS, Constants::Chair::SLOTS_PER_CHAIR);
        sem_.initialize(Semaphore::Index::EXIT_BIKE_TRAILS, Constants::Gate::EXIT_ROUTE_CAPACITY);
        sem_.initialize(Semaphore::Index::EXIT_WALKING_PATH, Constants::Gate::EXIT_ROUTE_CAPACITY);

        // Shared memory locks
        sem_.initialize(Semaphore::Index::SHM_OPERATIONAL, 1);
        sem_.initialize(Semaphore::Index::SHM_CHAIRS, 1);
        sem_.initialize(Semaphore::Index::SHM_STATS, 1);

        // Logging
        sem_.initialize(Semaphore::Index::LOG_SEQUENCE, 1);
        sem_.initialize(Semaphore::Index::LOG_QUEUE_SLOTS, Constants::Queue::LOG_QUEUE_CAPACITY);
    }

    /**
     * @brief Initialize shared state with simulation timing.
     * @param openTime Real time when simulation starts
     * @param closeTime Real time when simulation should end
     */
    void initState(time_t openTime, time_t closeTime) {
        state()->operational.state = RopewayState::RUNNING;
        state()->operational.acceptingNewTourists = true;
        state()->operational.openingTime = openTime;
        state()->operational.closingTime = closeTime;
        state()->stats.dailyStats.simulationStartTime = openTime;
    }

    /**
     * @brief Clean up all IPC resources.
     *
     * Safe to call multiple times. Destroys shared memory, semaphores,
     * and all message queues. Called automatically by destructor.
     */
    void cleanup() noexcept {
        if (cleanedUp_) return;
        cleanedUp_ = true;

        try { shm_.destroy(); } catch (...) {
        }
        try { sem_.destroy(); } catch (...) {
        }
        try { workerQueue_.destroy(); } catch (...) {
        }
        try { cashierQueue_.destroy(); } catch (...) {
        }
        try { entryGateQueue_.destroy(); } catch (...) {
        }
        try { logQueue_.destroy(); } catch (...) {
        }
        Logger::debug(Logger::Source::Other, tag_, "cleanup done");
    }

private:
    static constexpr auto tag_{"IpcManager"};

    key_t shmKey_;
    key_t semKey_;
    key_t workerMsgKey_;
    key_t cashierMsgKey_;
    key_t entryGateMsgKey_;
    key_t logMsgKey_;

    SharedMemory<SharedRopewayState> shm_;
    Semaphore sem_;
    MessageQueue<WorkerMessage> workerQueue_;
    MessageQueue<TicketRequest> cashierQueue_;
    MessageQueue<EntryGateRequest> entryGateQueue_;
    MessageQueue<LogMessage> logQueue_;
    bool cleanedUp_{false};
};

namespace IpcCleanup {
    inline void atexitHandler() {
        if (g_instance) {
            g_instance->cleanup();
        }
    }
}