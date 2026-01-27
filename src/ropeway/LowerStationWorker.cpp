#include <cstring>
#include <unistd.h>
#include <ctime>
#include <csignal>
#include <cstdlib>

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

    // Autonomous emergency detection parameters
    static constexpr uint32_t DANGER_CHECK_INTERVAL_SEC = 5; // Check for danger every N seconds
    static constexpr double DANGER_DETECTION_CHANCE = 0.05; // 5% chance per check to detect "danger"
    static constexpr uint32_t EMERGENCY_DURATION_SEC = 10; // Auto-resume after N seconds

    LowerWorkerProcess(const ArgumentParser::WorkerArgs &args)
        : shm_{SharedMemory<SharedRopewayState>::attach(args.shmKey)},
          sem_{args.semKey},
          msgQueue_{args.msgKey, "WorkerMsg"},
          entryRequestQueue_{args.entryGateMsgKey, "EntryReq"},
          entryResponseQueue_{args.entryGateMsgKey, "EntryResp"},
          isEmergencyStopped_{false},
          emergencyStartTime_{0},
          lastDangerCheckTime_{0},
          emergencyTriggeredAutonomously_{false} {
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
        srand(static_cast<unsigned>(time(nullptr)) ^ static_cast<unsigned>(getpid()));

        while (!g_signals.exit) {
            // Check for emergency signal from external source (backward compatibility)
            if (g_signals.emergency) {
                g_signals.emergency = 0;
                triggerEmergencyStop();
            }

            // Check for resume signal from external source
            if (g_signals.resume && isEmergencyStopped_) {
                g_signals.resume = 0;
                initiateResume();
            }

            // Check current state
            RopewayState currentState;
            bool isClosing;
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
                currentState = shm_->operational.state;
                isClosing = (currentState == RopewayState::CLOSING);
            }

            if (currentState == RopewayState::EMERGENCY_STOP) {
                // Check for auto-resume (if triggered autonomously)
                if (emergencyTriggeredAutonomously_ && emergencyStartTime_ > 0) {
                    time_t now = time(nullptr);
                    if (now - emergencyStartTime_ >= EMERGENCY_DURATION_SEC) {
                        Logger::info(TAG, "Auto-resuming after %u seconds", EMERGENCY_DURATION_SEC);
                        initiateResume();
                        continue;
                    }
                }
                // During emergency, brief sleep to check for auto-resume
                usleep(100000); // 100ms
                continue;
            }

            // Autonomous danger detection (only during normal operation, not closing)
            if (!isClosing && !isEmergencyStopped_) {
                checkForDanger();
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
            emergencyTriggeredAutonomously_ = false;
            emergencyStartTime_ = 0;
        } else {
            Logger::warn(TAG, "Timeout waiting for UpperWorker confirmation - resuming anyway");
            // Resume anyway to prevent permanent deadlock, but log the issue
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
                shm_->operational.state = RopewayState::RUNNING;
            }
            isEmergencyStopped_ = false;
            emergencyTriggeredAutonomously_ = false;
            emergencyStartTime_ = 0;
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

    /**
     * Autonomous danger detection.
     * Simulates worker detecting a problem (e.g., safety issue, malfunction).
     * Replaces centralized emergency timing from main process.
     */
    void checkForDanger() {
        time_t now = time(nullptr);

        // Only check periodically, not every iteration
        if (now - lastDangerCheckTime_ < DANGER_CHECK_INTERVAL_SEC) {
            return;
        }
        lastDangerCheckTime_ = now;

        // Random chance to detect "danger"
        double roll = static_cast<double>(rand()) / RAND_MAX;
        if (roll < DANGER_DETECTION_CHANCE) {
            Logger::warn(TAG, "!!! DANGER DETECTED - Initiating emergency stop !!!");
            emergencyTriggeredAutonomously_ = true;
            emergencyStartTime_ = now;
            triggerEmergencyStop();

            // Update statistics
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_STATS);
                shm_->stats.dailyStats.emergencyStops++;
            }
        }
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

    /**
     * Dispatch current chair and notify waiting tourists.
     */
    void dispatchChair(int32_t chairId, uint32_t* groupIndices, uint32_t groupCount,
                       BoardingQueue &queue, uint32_t totalSlots) {
        if (groupCount == 0) return;

        Chair &chair = shm_->chairPool.chairs[chairId];
        chair.isOccupied = true;
        chair.numPassengers = groupCount;
        chair.slotsUsed = totalSlots;
        chair.departureTime = time(nullptr);
        shm_->chairPool.chairsInUse++;

        for (uint32_t i = 0; i < groupCount; ++i) {
            BoardingQueueEntry &entry = queue.entries[groupIndices[i]];
            entry.assignedChairId = chairId;
            entry.readyToBoard = true;
            if (i < 4) {
                chair.passengerIds[i] = static_cast<int32_t>(entry.touristId);
            }
        }

        Logger::info(TAG, "Chair %d departing: %u groups, %u/4 slots",
                     chairId, groupCount, totalSlots);

        // Wake up tourists
        for (uint32_t i = 0; i < groupCount; ++i) {
            sem_.post(Semaphore::Index::CHAIR_ASSIGNED, false);
        }

        // Reset chair slots for next chair
        sem_.setValue(Semaphore::Index::CURRENT_CHAIR_SLOTS, Constants::Chair::SLOTS_PER_CHAIR);
    }

    /**
     * Simple FIFO boarding with slots.
     * Each tourist (group) has a pre-calculated slot count.
     * If tourist doesn't fit, dispatch current chair and wait for next.
     */
    void processBoardingQueue() {
        Semaphore::ScopedLock lockCore(sem_, Semaphore::Index::SHM_OPERATIONAL);
        Semaphore::ScopedLock lockChairs(sem_, Semaphore::Index::SHM_CHAIRS);

        if (shm_->operational.state == RopewayState::EMERGENCY_STOP) {
            return;
        }

        BoardingQueue &queue = shm_->chairPool.boardingQueue;
        if (queue.count == 0) return;

        if (shm_->chairPool.chairsInUse >= Constants::Chair::MAX_CONCURRENT_IN_USE) {
            return;
        }

        // Find available chair
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

        // Current chair state
        uint32_t slotsUsed = 0;
        uint32_t groupCount = 0;
        uint32_t groupIndices[4] = {0};

        // FIFO processing - simple slot-based boarding
        for (uint32_t i = 0; i < queue.count && groupCount < 4; ++i) {
            BoardingQueueEntry &entry = queue.entries[i];

            if (entry.readyToBoard || entry.assignedChairId >= 0) {
                continue;
            }

            // Check if this tourist's group fits
            if (slotsUsed + entry.slots <= Constants::Chair::SLOTS_PER_CHAIR) {
                // Fits - add to current chair
                groupIndices[groupCount++] = i;
                slotsUsed += entry.slots;

                // Log group composition
                if (entry.childCount > 0 && entry.hasBike) {
                    Logger::info(TAG, "Boarding Tourist %u: %s with bike + %u children (%u slots)",
                                 entry.touristId,
                                 entry.type == TouristType::CYCLIST ? "cyclist" : "pedestrian",
                                 entry.childCount, entry.slots);
                } else if (entry.childCount > 0) {
                    Logger::info(TAG, "Boarding Tourist %u: %s + %u children (%u slots)",
                                 entry.touristId,
                                 entry.type == TouristType::CYCLIST ? "cyclist" : "pedestrian",
                                 entry.childCount, entry.slots);
                } else if (entry.hasBike) {
                    Logger::info(TAG, "Boarding Tourist %u: cyclist with bike (%u slots)",
                                 entry.touristId, entry.slots);
                }

                // Chair full?
                if (slotsUsed >= Constants::Chair::SLOTS_PER_CHAIR) {
                    break;
                }
            } else {
                // Doesn't fit - dispatch current chair (if not empty)
                if (groupCount > 0) {
                    dispatchChair(chairId, groupIndices, groupCount, queue, slotsUsed);
                    return; // Tourist waits for next iteration
                }
                // Empty chair but tourist still doesn't fit (slots > 4)?
                // This shouldn't happen, but handle gracefully
                if (entry.slots > Constants::Chair::SLOTS_PER_CHAIR) {
                    Logger::error(TAG, "Tourist %u needs %u slots (max %u) - cannot board!",
                                  entry.touristId, entry.slots, Constants::Chair::SLOTS_PER_CHAIR);
                    // Remove from queue
                    queue.removeTourist(i);
                    shm_->operational.touristsInLowerStation--;
                    sem_.post(Semaphore::Index::STATION_CAPACITY, false);
                    if (entry.touristPid > 0) {
                        kill(entry.touristPid, SIGTERM);
                    }
                    i--; // Adjust index after removal
                }
            }
        }

        // Dispatch chair if we have passengers
        if (groupCount > 0) {
            dispatchChair(chairId, groupIndices, groupCount, queue, slotsUsed);
        }
    }

    SharedMemory<SharedRopewayState> shm_;
    Semaphore sem_;
    MessageQueue<WorkerMessage> msgQueue_;
    MessageQueue<EntryGateRequest> entryRequestQueue_;
    MessageQueue<EntryGateResponse> entryResponseQueue_;
    bool isEmergencyStopped_;
    time_t emergencyStartTime_;
    time_t lastDangerCheckTime_;
    bool emergencyTriggeredAutonomously_;
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
