#include <cstring>
#include <unistd.h>
#include <ctime>
#include <csignal>

#include "ipc/core/SharedMemory.hpp"
#include "ipc/core/Semaphore.hpp"
#include "ipc/core/MessageQueue.hpp"
#include "ipc/RopewaySystemState.hpp"
#include "ipc/message/WorkerMessage.hpp"
#include "Config.hpp"
#include "utils/SignalHelper.hpp"
#include "utils/ArgumentParser.hpp"
#include "utils/Logger.hpp"

namespace {
    SignalHelper::Flags g_signals;
    constexpr const char* TAG = "LowerWorker";
}

class LowerWorkerProcess {
public:
    static constexpr long MSG_TYPE_TO_UPPER = 2;
    static constexpr long MSG_TYPE_FROM_UPPER = 1;

    LowerWorkerProcess(const ArgumentParser::WorkerArgs& args)
        : shm_{SharedMemory<RopewaySystemState>::attach(args.shmKey)},
          sem_{args.semKey},
          msgQueue_{args.msgKey, "WorkerMsg"},
          isEmergencyStopped_{false} {

        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
            shm_->core.lowerWorkerPid = getpid();
        }

        Logger::info(TAG, "Started (PID: %d)", getpid());
        sem_.post(Semaphore::Index::LOWER_WORKER_READY, false);
    }

    void run() {
        Logger::info(TAG, "Beginning operations");

        while (!g_signals.exit) {
            // Check for emergency signal
            if (g_signals.emergency) {
                g_signals.emergency = 0;
                triggerEmergencyStop();
            }

            // Check for resume signal
            if (g_signals.resume && isEmergencyStopped_) {
                g_signals.resume = 0;
                initiateResume();
            }

            // Check current state
            RopewayState currentState;
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
                currentState = shm_->core.state;
            }

            if (currentState == RopewayState::EMERGENCY_STOP) {
                // During emergency, just wait for signals
                usleep(100000);
                continue;
            }

            // Normal operation - wait for work
            // Use tryWait to periodically check for signals
            if (sem_.tryWait(Semaphore::Index::BOARDING_QUEUE_WORK)) {
                if (!g_signals.exit && !g_signals.emergency) {
                    processBoardingQueue();
                }
            }

            logStatus();
        }

        Logger::info(TAG, "Shutting down");
    }

private:
    void triggerEmergencyStop() {
        Logger::warn(TAG, "!!! EMERGENCY STOP TRIGGERED !!!");

        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
            shm_->core.state = RopewayState::EMERGENCY_STOP;
        }

        isEmergencyStopped_ = true;

        // Notify UpperWorker
        sendMessage(WorkerSignal::EMERGENCY_STOP, "Emergency stop by LowerWorker");

        // Also send signal to UpperWorker
        pid_t upperWorkerPid;
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
            upperWorkerPid = shm_->core.upperWorkerPid;
        }
        if (upperWorkerPid > 0) {
            kill(upperWorkerPid, SIGUSR1);
        }

        Logger::info(TAG, "Emergency stop activated, waiting for resume (SIGUSR2)");
    }

    void initiateResume() {
        Logger::info(TAG, "Resume requested, checking with UpperWorker...");

        // Send ready message to UpperWorker
        sendMessage(WorkerSignal::READY_TO_START, "LowerWorker ready to resume");

        // Wake up UpperWorker with signal
        pid_t upperWorkerPid;
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
            upperWorkerPid = shm_->core.upperWorkerPid;
        }
        if (upperWorkerPid > 0) {
            kill(upperWorkerPid, SIGUSR2);
        }

        // Wait for UpperWorker confirmation
        Logger::info(TAG, "Waiting for UpperWorker confirmation...");
        auto response = msgQueue_.receive(MSG_TYPE_FROM_UPPER);

        if (response && response->signal == WorkerSignal::READY_TO_START) {
            Logger::info(TAG, "UpperWorker confirmed ready. Resuming operations!");

            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
                shm_->core.state = RopewayState::RUNNING;
            }

            isEmergencyStopped_ = false;
        } else {
            Logger::warn(TAG, "Did not receive confirmation from UpperWorker");
        }
    }

    void sendMessage(WorkerSignal signal, const char* text) {
        WorkerMessage msg;
        msg.senderId = 1;
        msg.receiverId = 2;
        msg.signal = signal;
        msg.timestamp = time(nullptr);
        strncpy(msg.messageText, text, sizeof(msg.messageText) - 1);
        msg.messageText[sizeof(msg.messageText) - 1] = '\0';

        msgQueue_.send(msg, MSG_TYPE_TO_UPPER);
    }

    void logStatus() {
        static time_t lastLog = 0;
        time_t now = time(nullptr);
        if (now - lastLog >= 3) {
            uint32_t queueCount, chairsInUse;
            RopewayState state;
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
                queueCount = shm_->chairPool.boardingQueue.count;
                chairsInUse = shm_->chairPool.chairsInUse;
                state = shm_->core.state;
            }

            if (state == RopewayState::EMERGENCY_STOP) {
                Logger::warn(TAG, "EMERGENCY STOP - Queue=%u, Chairs=%u/%u",
                             queueCount, chairsInUse, Config::Chair::MAX_CONCURRENT_IN_USE);
            } else {
                Logger::info(TAG, "Queue=%u, ChairsInUse=%u/%u",
                             queueCount, chairsInUse, Config::Chair::MAX_CONCURRENT_IN_USE);
            }
            lastLog = now;
        }
    }

    uint32_t getSlotCost(const BoardingQueueEntry& entry) {
        return (entry.type == TouristType::CYCLIST)
            ? Config::Chair::CYCLIST_SLOT_COST
            : Config::Chair::PEDESTRIAN_SLOT_COST;
    }

    /**
     * Find children of a guardian in the queue.
     * Returns number of children found, fills childIndices array.
     */
    uint32_t findChildren(BoardingQueue& queue, uint32_t guardianId,
                          uint32_t childIndices[], uint32_t maxChildren) {
        uint32_t found = 0;
        for (uint32_t i = 0; i < queue.count && found < maxChildren; ++i) {
            BoardingQueueEntry& entry = queue.entries[i];
            if (entry.guardianId == static_cast<int32_t>(guardianId) &&
                entry.assignedChairId < 0 && !entry.readyToBoard) {
                childIndices[found++] = i;
            }
        }
        return found;
    }

    void processBoardingQueue() {
        Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);

        // Don't process during emergency
        if (shm_->core.state == RopewayState::EMERGENCY_STOP) {
            return;
        }

        BoardingQueue& queue = shm_->chairPool.boardingQueue;
        if (queue.count == 0) return;

        if (shm_->chairPool.chairsInUse >= Config::Chair::MAX_CONCURRENT_IN_USE) {
            return;
        }

        uint32_t slotsUsed = 0;
        uint32_t groupSize = 0;
        uint32_t groupIndices[4] = {0};
        bool addedToGroup[BoardingQueue::MAX_SIZE] = {false};

        // First pass: find guardians with children and add them as family units
        for (uint32_t i = 0; i < queue.count && groupSize < 4; ++i) {
            BoardingQueueEntry& entry = queue.entries[i];

            if (entry.readyToBoard || entry.assignedChairId >= 0 || addedToGroup[i]) {
                continue;
            }

            // Skip children waiting for guardian (they'll be added with their guardian)
            if (entry.needsSupervision) {
                continue;
            }

            // Check if this is a guardian with children
            if (entry.dependentCount > 0) {
                uint32_t childIndices[Config::Gate::MAX_CHILDREN_PER_ADULT];
                uint32_t numChildren = findChildren(queue, entry.touristId,
                                                     childIndices, entry.dependentCount);

                // Wait until ALL children are in the queue before boarding
                if (numChildren < entry.dependentCount) {
                    // Not all children arrived yet, skip this guardian
                    continue;
                }

                // Calculate total slots for family
                uint32_t familySlots = getSlotCost(entry);
                for (uint32_t c = 0; c < numChildren; ++c) {
                    familySlots += getSlotCost(queue.entries[childIndices[c]]);
                }

                // Check if family fits
                if (slotsUsed + familySlots <= Config::Chair::SLOTS_PER_CHAIR &&
                    groupSize + 1 + numChildren <= 4) {

                    // Add guardian
                    groupIndices[groupSize++] = i;
                    addedToGroup[i] = true;
                    slotsUsed += getSlotCost(entry);

                    // Add children
                    for (uint32_t c = 0; c < numChildren; ++c) {
                        groupIndices[groupSize++] = childIndices[c];
                        addedToGroup[childIndices[c]] = true;
                        slotsUsed += getSlotCost(queue.entries[childIndices[c]]);
                    }

                    Logger::info(TAG, "[FAMILY] Guardian %u boarding with %u children",
                                 entry.touristId, numChildren);
                }
                continue;
            }

            // Regular tourist without children
            uint32_t slotCost = getSlotCost(entry);
            if (slotsUsed + slotCost <= Config::Chair::SLOTS_PER_CHAIR) {
                groupIndices[groupSize++] = i;
                addedToGroup[i] = true;
                slotsUsed += slotCost;
            }
        }

        if (groupSize == 0) return;

        int32_t chairId = -1;
        for (uint32_t c = 0; c < Config::Chair::QUANTITY; ++c) {
            uint32_t idx = (queue.nextChairId + c) % Config::Chair::QUANTITY;
            if (!shm_->chairPool.chairs[idx].isOccupied) {
                chairId = static_cast<int32_t>(idx);
                queue.nextChairId = (idx + 1) % Config::Chair::QUANTITY;
                break;
            }
        }

        if (chairId < 0) return;

        Chair& chair = shm_->chairPool.chairs[chairId];
        chair.isOccupied = true;
        chair.numPassengers = groupSize;
        chair.slotsUsed = slotsUsed;
        chair.departureTime = time(nullptr);
        shm_->chairPool.chairsInUse++;

        for (uint32_t i = 0; i < groupSize; ++i) {
            BoardingQueueEntry& entry = queue.entries[groupIndices[i]];
            entry.assignedChairId = chairId;
            entry.readyToBoard = true;
            if (i < 4) {
                chair.passengerIds[i] = static_cast<int32_t>(entry.touristId);
            }
        }

        Logger::info(TAG, "Chair %d assigned to %u tourists", chairId, groupSize);

        for (uint32_t i = 0; i < groupSize; ++i) {
            sem_.post(Semaphore::Index::CHAIR_ASSIGNED, false);
        }
    }

    SharedMemory<RopewaySystemState> shm_;
    Semaphore sem_;
    MessageQueue<WorkerMessage> msgQueue_;
    bool isEmergencyStopped_;
};

int main(int argc, char* argv[]) {
    ArgumentParser::WorkerArgs args{};
    if (!ArgumentParser::parseWorkerArgs(argc, argv, args)) {
        return 1;
    }

    SignalHelper::setup(g_signals, true);

    try {
        LowerWorkerProcess worker(args);
        worker.run();
    } catch (const std::exception& e) {
        Logger::error(TAG, "Exception: %s", e.what());
        return 1;
    }

    return 0;
}
