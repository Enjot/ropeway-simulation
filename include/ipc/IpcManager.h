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
#include "core/Config.h"
#include "logging/Logger.h"

class IpcManager;

namespace IpcCleanup {
    inline IpcManager* g_instance = nullptr;
    void atexitHandler();
}

class IpcManager {
public:
    IpcManager()
        : shmKey_{ftok(".", 'S')},
          semKey_{ftok(".", 'M')},
          workerMsgKey_{ftok(".", 'W')},
          cashierMsgKey_{ftok(".", 'C')},
          entryGateMsgKey_{ftok(".", 'E')},
          shm_{SharedMemory<SharedRopewayState>::create(shmKey_)},
          sem_{semKey_},
          workerQueue_{workerMsgKey_, "WorkerMessageQueue"},
          cashierQueue_{cashierMsgKey_, "CashierMessageQueue"},
          entryGateQueue_{entryGateMsgKey_, "EntryGateQueue"} {
        if (shmKey_ == -1 || semKey_ == -1 || workerMsgKey_ == -1 || cashierMsgKey_ == -1 || entryGateMsgKey_ == -1) {
            throw ipc_exception("ftok failed");
        }

        IpcCleanup::g_instance = this;
        std::atexit(IpcCleanup::atexitHandler);

        Logger::debug(tag_, "created");
    }

    ~IpcManager() {
        cleanup();
        IpcCleanup::g_instance = nullptr;
    }

    IpcManager(const IpcManager &) = delete;

    IpcManager &operator=(const IpcManager &) = delete;

    IpcManager(IpcManager &&) = delete;

    IpcManager &operator=(IpcManager &&) = delete;

    // State access
    SharedRopewayState *state() { return shm_.get(); }
    SharedRopewayState *operator->() { return shm_.get(); }

    // Resource access
    Semaphore &sem() { return sem_; }
    MessageQueue<WorkerMessage> &workerQueue() { return workerQueue_; }
    MessageQueue<TicketRequest> &cashierQueue() { return cashierQueue_; }
    MessageQueue<EntryGateRequest> &entryGateQueue() { return entryGateQueue_; }

    // Keys for child processes
    key_t shmKey() const { return shmKey_; }
    key_t semKey() const { return semKey_; }
    key_t workerMsgKey() const { return workerMsgKey_; }
    key_t cashierMsgKey() const { return cashierMsgKey_; }
    key_t entryGateMsgKey() const { return entryGateMsgKey_; }

    void initSemaphores(const uint16_t stationCapacity = Config::Gate::MAX_TOURISTS_ON_STATION()) const {
        sem_.initialize(Semaphore::Index::ENTRY_GATES, Constants::Gate::NUM_ENTRY_GATES);
        sem_.initialize(Semaphore::Index::RIDE_GATES, Constants::Gate::NUM_RIDE_GATES);
        sem_.initialize(Semaphore::Index::STATION_CAPACITY, stationCapacity);
        sem_.initialize(Semaphore::Index::CHAIR_ALLOCATION, 1);
        sem_.initialize(Semaphore::Index::SHM_OPERATIONAL, 1);
        sem_.initialize(Semaphore::Index::SHM_CHAIRS, 1);
        sem_.initialize(Semaphore::Index::SHM_STATS, 1);
        sem_.initialize(Semaphore::Index::WORKER_SYNC, 0);
        sem_.initialize(Semaphore::Index::CASHIER_READY, 0);
        sem_.initialize(Semaphore::Index::LOWER_WORKER_READY, 0);
        sem_.initialize(Semaphore::Index::UPPER_WORKER_READY, 0);
        sem_.initialize(Semaphore::Index::CHAIR_ASSIGNED, 0);
        sem_.initialize(Semaphore::Index::BOARDING_QUEUE_WORK, 0);
        sem_.initialize(Semaphore::Index::ENTRY_QUEUE_WORK, 0);
        // Upper station exit routes (one-way traffic, operating simultaneously)
        sem_.initialize(Semaphore::Index::EXIT_BIKE_TRAILS, Constants::Gate::EXIT_ROUTE_CAPACITY);
        sem_.initialize(Semaphore::Index::EXIT_WALKING_PATH, Constants::Gate::EXIT_ROUTE_CAPACITY);
    }

    void initState(time_t openTime, time_t closeTime) {
        state()->operational.state = RopewayState::RUNNING;
        state()->operational.acceptingNewTourists = true;
        state()->operational.openingTime = openTime;
        state()->operational.closingTime = closeTime;
        state()->stats.dailyStats.simulationStartTime = openTime;
    }

    void cleanup() noexcept {
        if (cleanedUp_) return;
        cleanedUp_ = true;

        try { shm_.destroy(); } catch (...) {}
        try { sem_.destroy(); } catch (...) {}
        try { workerQueue_.destroy(); } catch (...) {}
        try { cashierQueue_.destroy(); } catch (...) {}
        try { entryGateQueue_.destroy(); } catch (...) {}
        Logger::debug(tag_, "cleanup done");
    }

private:
    static constexpr auto tag_{"IpcManager"};

    key_t shmKey_;
    key_t semKey_;
    key_t workerMsgKey_;
    key_t cashierMsgKey_;
    key_t entryGateMsgKey_;

    SharedMemory<SharedRopewayState> shm_;
    Semaphore sem_;
    MessageQueue<WorkerMessage> workerQueue_;
    MessageQueue<TicketRequest> cashierQueue_;
    MessageQueue<EntryGateRequest> entryGateQueue_;
    bool cleanedUp_{false};
};

namespace IpcCleanup {
    inline void atexitHandler() {
        if (g_instance) {
            g_instance->cleanup();
        }
    }
}
