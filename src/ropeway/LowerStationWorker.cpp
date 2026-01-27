#include <cstring>
#include <unistd.h>
#include <ctime>
#include <csignal>

#include "ipc/core/SharedMemory.h"
#include "ipc/core/Semaphore.h"
#include "ipc/core/MessageQueue.h"
#include "ipc/model/SharedRopewayState.h"
#include "ropeway/worker/WorkerMessage.h"
#include "ropeway/gate/EntryGateMessage.h"
#include "core/Config.h"
#include "utils/SignalHelper.h"
#include "utils/ArgumentParser.h"
#include "logging/Logger.h"

namespace {
    SignalHelper::Flags g_signals;
    constexpr const char *TAG = "LowerWorker";
}

class LowerWorkerProcess {
public:
    static constexpr long MSG_TYPE_TO_UPPER = 2;
    static constexpr long MSG_TYPE_FROM_UPPER = 1;

    LowerWorkerProcess(const ArgumentParser::WorkerArgs &args)
        : shm_{SharedMemory<SharedRopewayState>::attach(args.shmKey)},
          sem_{args.semKey},
          msgQueue_{args.msgKey, "WorkerMsg"},
          entryRequestQueue_{args.entryGateMsgKey, "EntryReq"},
          entryResponseQueue_{args.entryGateMsgKey, "EntryResp"},
          isEmergencyStopped_{false} {
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            shm_->operational.lowerWorkerPid = getpid();
            Logger::setSimulationStartTime(shm_->operational.openingTime);
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
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
                currentState = shm_->operational.state;
            }

            if (currentState == RopewayState::EMERGENCY_STOP) {
                // During emergency, block waiting for resume signal
                // waitInterruptible returns false when interrupted by signal
                sem_.waitInterruptible(Semaphore::Index::WORKER_SYNC);
                continue;
            }

            // Block waiting for work (unified signal for both entry and boarding)
            // This is the main blocking point - no CPU spinning
            // Signals (SIGUSR1/SIGUSR2/SIGTERM) will interrupt and return false
            if (sem_.waitInterruptible(Semaphore::Index::BOARDING_QUEUE_WORK)) {
                if (!g_signals.exit && !g_signals.emergency) {
                    // Process entry queue first (handles incoming tourists)
                    processEntryQueue();
                    // Then process boarding queue (assigns chairs)
                    processBoardingQueue();
                }
            }

            logStatus();
        }

        Logger::warn(TAG, "Shutting down");
    }

private:
    void triggerEmergencyStop() {
        Logger::warn(TAG, "!!! EMERGENCY STOP TRIGGERED !!!");

        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            shm_->operational.state = RopewayState::EMERGENCY_STOP;
        }

        isEmergencyStopped_ = true;

        // Notify UpperWorker
        sendMessage(WorkerSignal::EMERGENCY_STOP, "Emergency stop by LowerWorker");

        // Also send signal to UpperWorker
        pid_t upperWorkerPid;
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            upperWorkerPid = shm_->operational.upperWorkerPid;
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
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            upperWorkerPid = shm_->operational.upperWorkerPid;
        }
        if (upperWorkerPid > 0) {
            kill(upperWorkerPid, SIGUSR2);
        }

        // Wait for UpperWorker confirmation with timeout (prevents indefinite hang)
        static constexpr uint32_t HANDSHAKE_TIMEOUT_SEC = 5;
        Logger::info(TAG, "Waiting for UpperWorker confirmation (timeout: %us)...", HANDSHAKE_TIMEOUT_SEC);
        auto response = msgQueue_.receiveWithTimeout(MSG_TYPE_FROM_UPPER, HANDSHAKE_TIMEOUT_SEC);

        if (response && response->signal == WorkerSignal::READY_TO_START) {
            Logger::info(TAG, "UpperWorker confirmed ready. Resuming operations!");

            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
                shm_->operational.state = RopewayState::RUNNING;
            }

            isEmergencyStopped_ = false;
        } else {
            Logger::warn(TAG, "Timeout waiting for UpperWorker confirmation - resuming anyway");
            // Resume anyway to prevent permanent deadlock, but log the issue
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
                shm_->operational.state = RopewayState::RUNNING;
            }
            isEmergencyStopped_ = false;
        }
    }

    void sendMessage(WorkerSignal signal, const char *text) {
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
                // Lock ordering: SHM_OPERATIONAL first, then SHM_CHAIRS
                Semaphore::ScopedLock lockCore(sem_, Semaphore::Index::SHM_OPERATIONAL);
                Semaphore::ScopedLock lockChairs(sem_, Semaphore::Index::SHM_CHAIRS);
                queueCount = shm_->chairPool.boardingQueue.count;
                chairsInUse = shm_->chairPool.chairsInUse;
                state = shm_->operational.state;
            }

            if (state == RopewayState::EMERGENCY_STOP) {
                Logger::warn(TAG, "EMERGENCY STOP - Queue=%u, Chairs=%u/%u",
                             queueCount, chairsInUse, Constants::Chair::MAX_CONCURRENT_IN_USE);
            } else if (state == RopewayState::CLOSING) {
                Logger::info(TAG, "CLOSING - Queue=%u, ChairsInUse=%u/%u (draining)",
                             queueCount, chairsInUse, Constants::Chair::MAX_CONCURRENT_IN_USE);
            } else {
                Logger::info(TAG, "Queue=%u, ChairsInUse=%u/%u",
                             queueCount, chairsInUse, Constants::Chair::MAX_CONCURRENT_IN_USE);
            }
            lastLog = now;
        }
    }

    /**
     * Process entry queue with VIP priority.
     * Uses negative mtype to receive lowest type first (VIP = 1, Regular = 2).
     */
    void processEntryQueue() {
        // Receive with negative mtype: gets lowest mtype first = VIP priority
        auto request = entryRequestQueue_.tryReceive(EntryGateMsgType::PRIORITY_RECEIVE);
        if (!request) {
            return;
        }

        // Determine which queue slot semaphore to release when done
        uint8_t queueSlotSem = request->isVip
            ? Semaphore::Index::ENTRY_QUEUE_VIP_SLOTS
            : Semaphore::Index::ENTRY_QUEUE_REGULAR_SLOTS;

        EntryGateResponse response;
        response.touristId = request->touristId;

        // Check if ropeway is accepting
        bool accepting;
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            accepting = shm_->operational.acceptingNewTourists;
        }

        if (!accepting) {
            response.allowed = false;
            long responseType = EntryGateMsgType::RESPONSE_BASE + request->touristId;
            entryResponseQueue_.send(response, responseType);
            sem_.post(queueSlotSem, false); // Release queue slot
            Logger::info(TAG, "Entry denied for Tourist %u: closed", request->touristId);
            return;
        }

        // Try to acquire station capacity (non-blocking resource check)
        if (!sem_.tryAcquire(Semaphore::Index::STATION_CAPACITY)) {
            // Station full, put request back in queue and try again later
            // Preserve priority: VIP requests get lower mtype
            // NOTE: Do NOT release queue slot - tourist is still waiting
            long reqType = request->isVip ? EntryGateMsgType::VIP_REQUEST : EntryGateMsgType::REGULAR_REQUEST;
            entryRequestQueue_.send(*request, reqType);
            sem_.post(Semaphore::Index::BOARDING_QUEUE_WORK, false); // Re-signal unified work queue
            return;
        }

        // Station has capacity, allow entry
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            shm_->operational.touristsInLowerStation++;
        }

        response.allowed = true;
        long responseType = EntryGateMsgType::RESPONSE_BASE + request->touristId;
        entryResponseQueue_.send(response, responseType);
        sem_.post(queueSlotSem, false); // Release queue slot

        Logger::info(TAG, "Entry granted to Tourist %u%s",
                     request->touristId, request->isVip ? " [VIP]" : "");
    }

    uint32_t getSlotCost(const BoardingQueueEntry &entry) {
        return (entry.type == TouristType::CYCLIST)
                   ? Constants::Chair::CYCLIST_SLOT_COST
                   : Constants::Chair::PEDESTRIAN_SLOT_COST;
    }

    /**
     * Find children of a guardian in the queue.
     * Returns number of children found, fills childIndices array.
     */
    uint32_t findChildren(BoardingQueue &queue, uint32_t guardianId,
                          uint32_t childIndices[], uint32_t maxChildren) {
        uint32_t found = 0;
        for (uint32_t i = 0; i < queue.count && found < maxChildren; ++i) {
            BoardingQueueEntry &entry = queue.entries[i];
            if (entry.guardianId == static_cast<int32_t>(guardianId) &&
                entry.assignedChairId < 0 && !entry.readyToBoard) {
                childIndices[found++] = i;
            }
        }
        return found;
    }

    void processBoardingQueue() {
        // Lock ordering: SHM_OPERATIONAL first, then SHM_CHAIRS
        Semaphore::ScopedLock lockCore(sem_, Semaphore::Index::SHM_OPERATIONAL);
        Semaphore::ScopedLock lockChairs(sem_, Semaphore::Index::SHM_CHAIRS);

        // Don't process during emergency
        if (shm_->operational.state == RopewayState::EMERGENCY_STOP) {
            return;
        }

        BoardingQueue &queue = shm_->chairPool.boardingQueue;
        if (queue.count == 0) return;

        if (shm_->chairPool.chairsInUse >= Constants::Chair::MAX_CONCURRENT_IN_USE) {
            return;
        }

        uint32_t slotsUsed = 0;
        uint32_t groupSize = 0;
        uint32_t groupIndices[4] = {0};
        bool addedToGroup[BoardingQueue::MAX_SIZE] = {false};

        // Helper: check if guardian is present in queue
        auto isGuardianInQueue = [&queue](int32_t guardianId) -> bool {
            if (guardianId < 0) return false;
            for (uint32_t j = 0; j < queue.count; ++j) {
                if (queue.entries[j].touristId == static_cast<uint32_t>(guardianId)) {
                    return true;
                }
            }
            return false;
        };

        // First pass: find guardians with children and add them as family units
        for (uint32_t i = 0; i < queue.count && groupSize < 4; ++i) {
            BoardingQueueEntry &entry = queue.entries[i];

            if (entry.readyToBoard || entry.assignedChairId >= 0 || addedToGroup[i]) {
                continue;
            }

            // Skip children waiting for guardian ONLY if guardian is actually in queue
            // If guardian has left (not in queue), child can board alone (orphan handling)
            if (entry.needsSupervision && isGuardianInQueue(entry.guardianId)) {
                continue;
            }

            // Check if this is a guardian with children
            if (entry.dependentCount > 0) {
                uint32_t childIndices[Constants::Gate::MAX_CHILDREN_PER_ADULT];
                uint32_t numChildren = findChildren(queue, entry.touristId,
                                                    childIndices, entry.dependentCount);

                // If children are waiting in queue, board with them
                // Note: Some children may have already boarded as orphans - that's OK
                if (numChildren > 0) {
                    // Calculate total slots for family
                    uint32_t familySlots = getSlotCost(entry);
                    for (uint32_t c = 0; c < numChildren; ++c) {
                        familySlots += getSlotCost(queue.entries[childIndices[c]]);
                    }

                    // Check if family fits
                    if (slotsUsed + familySlots <= Constants::Chair::SLOTS_PER_CHAIR &&
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

                // No children waiting - they may have all boarded as orphans
                // Guardian can board alone (fall through to regular tourist handling)
            }

            // Regular tourist without children (or orphaned child whose guardian left)
            uint32_t slotCost = getSlotCost(entry);
            if (slotsUsed + slotCost <= Constants::Chair::SLOTS_PER_CHAIR) {
                if (entry.needsSupervision) {
                    Logger::info(TAG, "[ORPHAN] Child %u boarding alone (guardian %d left)",
                                 entry.touristId, entry.guardianId);
                }
                groupIndices[groupSize++] = i;
                addedToGroup[i] = true;
                slotsUsed += slotCost;
            }
        }

        if (groupSize == 0) return;

        int32_t chairId = -1;
        for (uint32_t c = 0; c < Constants::Chair::QUANTITY; ++c) {
            uint32_t idx = (queue.nextChairId + c) % Constants::Chair::QUANTITY;
            if (!shm_->chairPool.chairs[idx].isOccupied) {
                chairId = static_cast<int32_t>(idx);
                queue.nextChairId = (idx + 1) % Constants::Chair::QUANTITY;
                break;
            }
        }

        if (chairId < 0) return;

        Chair &chair = shm_->chairPool.chairs[chairId];
        chair.isOccupied = true;
        chair.numPassengers = groupSize;
        chair.slotsUsed = slotsUsed;
        chair.departureTime = time(nullptr);
        shm_->chairPool.chairsInUse++;

        for (uint32_t i = 0; i < groupSize; ++i) {
            BoardingQueueEntry &entry = queue.entries[groupIndices[i]];
            entry.assignedChairId = chairId;
            entry.readyToBoard = true;
            if (i < 4) {
                chair.passengerIds[i] = static_cast<int32_t>(entry.touristId);
            }
        }

        Logger::info(TAG, "Chair %d assigned to %u tourists (ChairsInUse: %u/%u)",
                     chairId, groupSize, shm_->chairPool.chairsInUse, Constants::Chair::MAX_CONCURRENT_IN_USE);

        for (uint32_t i = 0; i < groupSize; ++i) {
            sem_.post(Semaphore::Index::CHAIR_ASSIGNED, false);
        }
    }

    SharedMemory<SharedRopewayState> shm_;
    Semaphore sem_;
    MessageQueue<WorkerMessage> msgQueue_;
    MessageQueue<EntryGateRequest> entryRequestQueue_;
    MessageQueue<EntryGateResponse> entryResponseQueue_;
    bool isEmergencyStopped_;
};

int main(int argc, char *argv[]) {
    ArgumentParser::WorkerArgs args{};
    if (!ArgumentParser::parseWorkerArgs(argc, argv, args)) {
        return 1;
    }

    SignalHelper::setup(g_signals, true);

    try {
        LowerWorkerProcess worker(args);
        worker.run();
    } catch (const std::exception &e) {
        Logger::error(TAG, "Exception: %s", e.what());
        return 1;
    }

    return 0;
}
