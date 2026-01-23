#pragma once

#include "ipc/core/SharedMemory.hpp"
#include "core/Semaphore.hpp"
#include "core/MessageQueue.hpp"
#include "ipc/RopewaySystemState.hpp"
#include "message/WorkerMessage.hpp"
#include "message/CashierMessage.hpp"
#include "../Config.hpp"

/**
 * Unified IPC resource manager for the ropeway simulation.
 *
 * Manages all System V IPC resources needed by processes:
 * - Shared memory segment for RopewaySystemState
 * - Semaphore set for synchronization (station capacity, gates, etc.)
 * - Message queue for worker-to-worker communication
 * - Message queue for tourist-cashier ticket requests
 *
 * Provides RAII-style lifecycle management - resources are created
 * or attached in constructor based on the 'create' flag.
 *
 * Usage (orchestrator/main - creates resources):
 *   IpcManager ipc(Config::Ipc::SHM_KEY_BASE, true);
 *   ipc.initializeSemaphores(stationCapacity);
 *   ipc.initializeState(openTime, closeTime);
 *
 * Usage (worker/tourist/cashier - attaches to existing):
 *   IpcManager ipc(shmKey, false);
 *   RopewaySystemState* state = ipc.state();
 */
class IpcManager {
public:
    /**
     * Create or attach to IPC resources.
     *
     * @param baseKey   Base key for shared memory (semaphore/message keys derived from Config)
     * @param create    If true, create new resources; if false, attach to existing
     * @param keyOffset Offset added to all keys (useful for running multiple test instances)
     */
    IpcManager(key_t baseKey, bool create, int keyOffset = 0)
        : shmKey_{baseKey + keyOffset},
          semKey_{static_cast<key_t>(Config::Ipc::SEM_KEY_BASE) + keyOffset},
          msgKey_{static_cast<key_t>(Config::Ipc::MSG_KEY_BASE) + keyOffset},
          cashierMsgKey_{static_cast<key_t>(Config::Ipc::MSG_KEY_BASE) + 1 + keyOffset},
          shm_{shmKey_, create},
          sem_{semKey_},
          workerMsgQueue_{msgKey_},
          cashierMsgQueue_{cashierMsgKey_} {
    }

    ~IpcManager() = default;

    // Non-copyable (IPC resources should not be duplicated)
    IpcManager(const IpcManager &) = delete;

    IpcManager &operator=(const IpcManager &) = delete;

    // ==================== State Access ====================

    /** Get pointer to shared ropeway system state. */
    RopewaySystemState *state() { return shm_.get(); }
    const RopewaySystemState *state() const { return shm_.get(); }

    /** Arrow operator for convenient state access: ipc->touristsInLowerStation */
    RopewaySystemState *operator->() { return shm_.get(); }

    // ==================== Semaphore Access ====================

    /** Get reference to semaphore set for synchronization operations. */
    Semaphore &semaphores() { return sem_; }
    const Semaphore &semaphores() const { return sem_; }

    // ==================== Message Queue Access ====================

    /** Get worker message queue (worker-to-worker communication). */
    MessageQueue<WorkerMessage> &workerQueue() { return workerMsgQueue_; }

    /** Get cashier message queue (tourist ticket requests/responses). */
    MessageQueue<TicketRequest> &cashierRequestQueue() { return cashierMsgQueue_; }

    // ==================== Initialization ====================

    /**
     * Initialize semaphore values for simulation.
     * Must be called by the creating process before spawning children.
     *
     * @param stationCapacity Maximum tourists allowed on lower station (N)
     */
    void initializeSemaphores(const uint16_t stationCapacity = Config::Gate::MAX_TOURISTS_ON_STATION) const {
        sem_.initialize(Semaphore::Index::STATION_CAPACITY, stationCapacity);
        sem_.initialize(Semaphore::Index::SHARED_MEMORY, 1);
        sem_.initialize(Semaphore::Index::ENTRY_GATES, Config::Gate::NUM_ENTRY_GATES);
        sem_.initialize(Semaphore::Index::RIDE_GATES, Config::Gate::NUM_RIDE_GATES);
        sem_.initialize(Semaphore::Index::CHAIR_ALLOCATION, 1);
        sem_.initialize(Semaphore::Index::WORKER_SYNC, 0);
        // Process readiness semaphores - initialized to 0, processes signal when ready
        sem_.initialize(Semaphore::Index::CASHIER_READY, 0);
        sem_.initialize(Semaphore::Index::LOWER_WORKER_READY, 0);
        sem_.initialize(Semaphore::Index::UPPER_WORKER_READY, 0);
    }

    /**
     * Initialize shared state with simulation time boundaries.
     *
     * @param openingTime Simulation start time (Tp)
     * @param closingTime Simulation end time (Tk) - gates stop accepting after this
     */
    void initializeState(time_t openingTime, time_t closingTime) {
        shm_->core.state = RopewayState::RUNNING;
        shm_->core.acceptingNewTourists = true;
        shm_->core.openingTime = openingTime;
        shm_->core.closingTime = closingTime;
        shm_->stats.dailyStats.simulationStartTime = openingTime;
    }

    // ==================== Key Accessors ====================
    // Used when spawning child processes that need to attach to IPC resources

    key_t shmKey() const { return shmKey_; }
    key_t semKey() const { return semKey_; }
    key_t msgKey() const { return msgKey_; }
    key_t cashierMsgKey() const { return cashierMsgKey_; }

    // ==================== Cleanup ====================

    /**
     * Remove all IPC resources for a given key set.
     * Should be called at program end or before creating new resources.
     * Safe to call even if resources don't exist.
     *
     * @param baseKey   Base shared memory key
     * @param keyOffset Offset that was used when creating resources
     */
    static void cleanup(key_t baseKey, int keyOffset = 0) {
        const key_t shmKey = baseKey + keyOffset;
        const key_t semKey = static_cast<key_t>(Config::Ipc::SEM_KEY_BASE) + keyOffset;
        const key_t msgKey = static_cast<key_t>(Config::Ipc::MSG_KEY_BASE) + keyOffset;
        const key_t cashierMsgKey = static_cast<key_t>(Config::Ipc::MSG_KEY_BASE) + 1 + keyOffset;

        SharedMemory<RopewaySystemState>::removeByKey(shmKey);
        Semaphore::destroy(semKey);
        MessageQueue<WorkerMessage>::removeByKey(msgKey);
        MessageQueue<TicketRequest>::removeByKey(cashierMsgKey);
    }

private:
    // IPC keys (stored for child process spawning)
    key_t shmKey_; // Shared memory key
    key_t semKey_; // Semaphore set key
    key_t msgKey_; // Worker message queue key
    key_t cashierMsgKey_; // Cashier message queue key

    // IPC resource wrappers (RAII - cleaned up on destruction)
    SharedMemory<RopewaySystemState> shm_;       // Ropeway state shared across all processes
    Semaphore sem_;                              // Semaphore set for synchronization
    MessageQueue<WorkerMessage> workerMsgQueue_; // Worker-to-worker signals
    MessageQueue<TicketRequest> cashierMsgQueue_; // Tourist ticket requests
};
