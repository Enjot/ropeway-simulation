#include <cstring>
#include <unistd.h>
#include <ctime>
#include <csignal>
#include <cstdlib>
#include <optional>

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
    constexpr auto SRC = Logger::Source::LowerWorker;
}

class LowerWorkerProcess {
public:
    static constexpr long MSG_TYPE_TO_UPPER = 2;
    static constexpr long MSG_TYPE_FROM_UPPER = 1;

    // Autonomous emergency detection parameters
    static constexpr uint32_t DANGER_CHECK_INTERVAL_SEC = 5; // Check for danger every N seconds
    static constexpr double DANGER_DETECTION_CHANCE = 0.10; // 10% chance per check to detect "danger"

    LowerWorkerProcess(const ArgumentParser::WorkerArgs &args)
        : shm_{SharedMemory<SharedRopewayState>::attach(args.shmKey)},
          sem_{args.semKey},
          msgQueue_{args.msgKey, "WorkerMsg"},
          entryRequestQueue_{args.entryGateMsgKey, "EntryReq"},
          entryResponseQueue_{args.entryGateMsgKey, "EntryResp"},
          isEmergencyStopped_{false},
          lastDangerCheckTime_{0},
          hasDetectedDanger_{false},
          pendingEntryRequest_{std::nullopt} {
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            shm_->operational.lowerWorkerPid = getpid();
            Logger::setSimulationStartTime(shm_->operational.openingTime);
        }

        Logger::info(SRC, TAG, "Started (PID: %d)", getpid());
        sem_.post(Semaphore::Index::LOWER_WORKER_READY, 1, false);
    }

    void run() {
        Logger::info(SRC, TAG, "Beginning operations");
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

                // Check who detected the danger to determine role
                pid_t detectorPid;
                {
                    Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
                    detectorPid = shm_->operational.dangerDetectorPid;
                }

                Logger::debug(SRC, TAG, "Resume check: detectorPid=%d, myPid=%d, isInitiator=%s",
                              detectorPid, getpid(), (detectorPid == getpid()) ? "yes" : "no");

                if (detectorPid == getpid()) {
                    // We detected the danger - we're already in initiateResume() from checkForDanger()
                    Logger::debug(SRC, TAG, "Resume signal ignored - we are the initiator");
                } else {
                    // Other worker detected danger - we're the responder
                    handleResumeRequest();
                }
            }

            // Check current state
            RopewayState currentState;
            bool isClosing;
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
                currentState = shm_->operational.state;
                isClosing = (currentState == RopewayState::CLOSING);
            }

            // Use local flag for emergency handling - shared state may have been
            // overwritten by Simulation (e.g., CLOSING transition during emergency)
            if (isEmergencyStopped_) {
                Logger::debug(SRC, TAG, "Emergency loop: sharedState=%d", static_cast<int>(currentState));
                // Block until a signal (SIGUSR2 for resume) interrupts the wait
                sem_.wait(Semaphore::Index::BOARDING_QUEUE_WORK, 1, false);
                continue;
            }

            // Autonomous danger detection (only during normal operation, not closing)
            if (!isClosing) {
                checkForDanger();
            }

            // If emergency was just triggered by checkForDanger(), skip blocking wait
            if (isEmergencyStopped_) {
                Logger::debug(SRC, TAG, "Emergency just triggered, skipping blocking wait");
                continue;
            }

            // Block waiting for work (unified signal for both entry and boarding)
            // This is the main blocking point - no CPU spinning
            // Signals (SIGUSR1/SIGUSR2/SIGTERM) will interrupt and return false
            Logger::debug(SRC, TAG, "Waiting for BOARDING_QUEUE_WORK (pending=%s)",
                          pendingEntryRequest_ ? "yes" : "no");
            if (sem_.wait(Semaphore::Index::BOARDING_QUEUE_WORK, 1, false)) {
                if (!g_signals.exit && !g_signals.emergency) {
                    Logger::debug(SRC, TAG, "Woke up: processing entry then boarding");
                    // Process entry queue first (handles incoming tourists)
                    processEntryQueue();
                    // Then process boarding queue (assigns chairs)
                    processBoardingQueue();
                }
            } else {
                Logger::debug(SRC, TAG, "BOARDING_QUEUE_WORK interrupted (exit=%d, emerg=%d)",
                              g_signals.exit ? 1 : 0, g_signals.emergency ? 1 : 0);
            }

            logStatus();
        }

        Logger::warn(SRC, TAG, "Lower station worker stopping - ending boarding operations");
    }

private:
    void triggerEmergencyStop() {
        // Record emergency start for statistics
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_STATS);
            currentEmergencyRecordIndex_ = shm_->stats.dailyStats.recordEmergencyStart(1); // workerId=1
        }

        Logger::warn(SRC, TAG, "!!! EMERGENCY STOP TRIGGERED !!!");

        pid_t upperWorkerPid;
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            Logger::debug(SRC, TAG, "triggerEmergencyStop: prevState=%d, accepting=%d",
                          static_cast<int>(shm_->operational.state),
                          shm_->operational.acceptingNewTourists ? 1 : 0);
            shm_->operational.state = RopewayState::EMERGENCY_STOP;
            shm_->operational.dangerDetectorPid = getpid();
            upperWorkerPid = shm_->operational.upperWorkerPid;
            Logger::debug(SRC, TAG, "triggerEmergencyStop: set dangerDetectorPid=%d", getpid());
        }

        isEmergencyStopped_ = true;

        // Notify UpperWorker
        sendMessage(WorkerSignal::EMERGENCY_STOP, "Emergency stop by LowerWorker");

        // Also send signal to UpperWorker
        if (upperWorkerPid > 0) {
            kill(upperWorkerPid, SIGUSR1);
        }

        Logger::info(SRC, TAG, "Emergency stop activated");
    }

    void handleResumeRequest() {
        Logger::info(SRC, TAG, "Resume signal received, confirming ready...");

        // Check for ready message from UpperWorker
        auto msg = msgQueue_.tryReceive(MSG_TYPE_FROM_UPPER);
        if (msg && msg->signal == WorkerSignal::READY_TO_START) {
            Logger::info(SRC, TAG, "UpperWorker ready, sending confirmation");
        }

        // Send confirmation back to UpperWorker
        sendMessage(WorkerSignal::READY_TO_START, "LowerWorker ready to resume");
        Logger::info(SRC, TAG, "Confirmation sent to UpperWorker");

        // Update state
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            if (shm_->operational.acceptingNewTourists) {
                shm_->operational.state = RopewayState::RUNNING;
                Logger::debug(SRC, TAG, "handleResumeRequest: state -> RUNNING");
            } else {
                shm_->operational.state = RopewayState::CLOSING;
                Logger::debug(SRC, TAG, "handleResumeRequest: state -> CLOSING (closing time reached during emergency)");
            }
            shm_->operational.dangerDetectorPid = 0; // Clear danger detector
        }

        // Record emergency end if this worker started it
        if (currentEmergencyRecordIndex_ >= 0) {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_STATS);
            shm_->stats.dailyStats.recordEmergencyEnd(currentEmergencyRecordIndex_);
            currentEmergencyRecordIndex_ = -1;
        }
        isEmergencyStopped_ = false;

        // Wake up main loop to resume processing boarding queue
        sem_.post(Semaphore::Index::BOARDING_QUEUE_WORK, 1, false);
    }

    void initiateResume() {
        Logger::info(SRC, TAG, "Resume requested, checking with UpperWorker...");

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

        // Wait for UpperWorker confirmation (both workers must confirm to resume)
        Logger::info(SRC, TAG, "Waiting for UpperWorker confirmation...");
        std::optional<WorkerMessage> response;
        while (!g_signals.exit) {
            response = msgQueue_.receiveInterruptible(MSG_TYPE_FROM_UPPER);
            if (response) break;
            // Signal interrupted - loop back and retry unless exiting
        }

        if (response && response->signal == WorkerSignal::READY_TO_START) {
            Logger::info(SRC, TAG, "UpperWorker confirmed ready. Resuming operations!");
        } else {
            Logger::debug(SRC, TAG, "Resume: no READY_TO_START response (exit=%d)", g_signals.exit ? 1 : 0);
        }

        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            // If closing time was reached during emergency, go to CLOSING instead of RUNNING
            if (shm_->operational.acceptingNewTourists) {
                shm_->operational.state = RopewayState::RUNNING;
                Logger::debug(SRC, TAG, "Resume: state -> RUNNING");
            } else {
                shm_->operational.state = RopewayState::CLOSING;
                Logger::debug(SRC, TAG, "Resume: state -> CLOSING (closing time reached during emergency)");
            }
            shm_->operational.dangerDetectorPid = 0; // Clear danger detector
        }

        // Record emergency end if this worker started it
        if (currentEmergencyRecordIndex_ >= 0) {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_STATS);
            shm_->stats.dailyStats.recordEmergencyEnd(currentEmergencyRecordIndex_);
            currentEmergencyRecordIndex_ = -1;
        }
        isEmergencyStopped_ = false;

        // Wake up main loop to resume processing boarding queue
        sem_.post(Semaphore::Index::BOARDING_QUEUE_WORK, 1, false);
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
        // Each worker can only detect danger once per simulation
        if (hasDetectedDanger_) {
            return;
        }

        time_t now = time(nullptr) - shm_->operational.totalPausedSeconds;

        // Only check periodically, not every iteration
        if (now - lastDangerCheckTime_ < DANGER_CHECK_INTERVAL_SEC) {
            return;
        }
        lastDangerCheckTime_ = now;

        // Random chance to detect "danger"
        double roll = static_cast<double>(rand()) / RAND_MAX;
        if (roll < DANGER_DETECTION_CHANCE) {
            hasDetectedDanger_ = true;
            Logger::warn(SRC, TAG, "!!! DANGER DETECTED - Initiating emergency stop !!!");
            triggerEmergencyStop();

            // Simulate time to assess and resolve the danger (3-6 real seconds)
            int resolveTime = 3 + (rand() % 4);
            int simulatedMinutes = resolveTime * Config::Simulation::TIME_SCALE() / 60;
            Logger::info(SRC, TAG, "Assessing danger... (estimated %d minutes)", simulatedMinutes);

            // Wait using simulation time (respects pause)
            time_t startSimTime = time(nullptr) - shm_->operational.totalPausedSeconds;
            while (!g_signals.exit) {
                time_t currentSimTime = time(nullptr) - shm_->operational.totalPausedSeconds;
                if (currentSimTime - startSimTime >= resolveTime) {
                    break;
                }
                sleep(1);
            }

            // Per requirements: worker who stopped the ropeway initiates resume
            // communication with the other worker
            initiateResume();
        }
    }

    void logStatus() {
        static time_t lastLog = 0;
        time_t now = time(nullptr) - shm_->operational.totalPausedSeconds;
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
                Logger::warn(SRC, TAG, "EMERGENCY STOP - Queue=%u, Chairs=%u/%u",
                             queueCount, chairsInUse, Constants::Chair::MAX_CONCURRENT_IN_USE);
            } else if (state == RopewayState::CLOSING) {
                Logger::info(SRC, TAG, "CLOSING - Queue=%u, ChairsInUse=%u/%u (draining)",
                             queueCount, chairsInUse, Constants::Chair::MAX_CONCURRENT_IN_USE);
            } else {
                Logger::info(SRC, TAG, "Queue=%u, ChairsInUse=%u/%u",
                             queueCount, chairsInUse, Constants::Chair::MAX_CONCURRENT_IN_USE);
            }
            lastLog = now;
        }
    }

    /**
     * Send entry response to tourist (blocking).
     * The tourist is waiting on receive, so this unblocks as soon as the
     * system-wide queue limit has room.
     */
    void sendEntryResponse(const EntryGateResponse &response, long responseType) {
        entryResponseQueue_.send(response, responseType);
    }

    /**
     * Process entry queue with VIP priority.
     * Uses negative mtype to receive lowest type first (VIP = 1, Regular = 2).
     *
     * Processes requests in a loop: handles all pending entries until the station
     * is full or the queue is empty. When the station is full, the current request
     * is kept in a local buffer (pendingEntryRequest_) rather than re-queued to
     * the message queue. This avoids a potential deadlock where msgsnd blocks
     * because the System V message queue is full and LowerWorker (the only
     * consumer) cannot drain it.
     */
    void processEntryQueue() {
        while (true) {
            // First try the locally buffered pending request, then the message queue
            EntryGateRequest request;
            bool fromPending = false;

            if (pendingEntryRequest_) {
                request = *pendingEntryRequest_;
                fromPending = true;
            } else {
                auto received = entryRequestQueue_.tryReceive(EntryGateMsgType::PRIORITY_RECEIVE);
                if (!received) {
                    return; // No more requests
                }
                request = *received;
            }

            uint8_t queueSlotSem = request.isVip
                                       ? Semaphore::Index::ENTRY_QUEUE_VIP_SLOTS
                                       : Semaphore::Index::ENTRY_QUEUE_REGULAR_SLOTS;

            // Check if ropeway is accepting
            bool accepting;
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
                accepting = shm_->operational.acceptingNewTourists;
            }

            if (!accepting) {
                EntryGateResponse response;
                response.touristId = request.touristId;
                response.allowed = false;
                long responseType = EntryGateMsgType::RESPONSE_BASE + request.touristId;
                sendEntryResponse(response, responseType);
                sem_.post(queueSlotSem, 1, false);
                Logger::info(SRC, TAG, "Entry denied for Tourist %u: closed", request.touristId);
                pendingEntryRequest_.reset();
                continue; // Process next request
            }

            // Try to acquire station capacity (non-blocking resource check)
            // useUndo=false: LowerWorker acquires but Tourist releases in rideChair()
            if (!sem_.tryAcquire(Semaphore::Index::STATION_CAPACITY, 1, false)) {
                // Station full - buffer locally instead of re-queueing to avoid
                // msgsnd deadlock (the message queue may be at capacity and we are
                // the only consumer). Tourist's BOARDING_QUEUE_WORK from rideChair()
                // will wake us to retry.
                if (!fromPending) {
                    pendingEntryRequest_ = request;
                }
                Logger::debug(SRC, TAG, "Station full, pending entry for Tourist %u", request.touristId);
                return;
            }

            // Station has capacity, allow entry
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
                shm_->operational.touristsInLowerStation++;
            }

            EntryGateResponse response;
            response.touristId = request.touristId;
            response.allowed = true;
            long responseType = EntryGateMsgType::RESPONSE_BASE + request.touristId;
            sendEntryResponse(response, responseType);
            sem_.post(queueSlotSem, 1, false);

            Logger::info(SRC, TAG, "Entry granted to Tourist %u%s",
                         request.touristId, request.isVip ? " [VIP]" : "");
            pendingEntryRequest_.reset();
            // Continue loop to process next request
        }
    }

    /**
     * Dispatch current chair and notify waiting tourists.
     */
    void dispatchChair(int32_t chairId, uint32_t *groupIndices, uint32_t groupCount,
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

        Logger::info(SRC, TAG, "Chair %d departing: %u groups, %u/4 slots",
                     chairId, groupCount, totalSlots);

        // Wake up ALL tourists in queue so they can check their assignment
        // This avoids a race condition where non-assigned tourists could starve assigned ones
        for (uint32_t i = 0; i < queue.count; ++i) {
            sem_.post(Semaphore::Index::CHAIR_ASSIGNED, 1, false);
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

        // Check if chairs available (non-blocking, semaphore-based)
        if (!sem_.tryAcquire(Semaphore::Index::CHAIRS_AVAILABLE, 1, false)) {
            return; // All 36 chairs in use, wait for tourist to release
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
        if (chairId < 0) {
            sem_.post(Semaphore::Index::CHAIRS_AVAILABLE, 1, false); // Release, no chair dispatched
            return;
        }

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
                    Logger::info(SRC, TAG, "Boarding Tourist %u: %s with bike + %u children (%u slots)",
                                 entry.touristId,
                                 entry.type == TouristType::CYCLIST ? "cyclist" : "pedestrian",
                                 entry.childCount, entry.slots);
                } else if (entry.childCount > 0) {
                    Logger::info(SRC, TAG, "Boarding Tourist %u: %s + %u children (%u slots)",
                                 entry.touristId,
                                 entry.type == TouristType::CYCLIST ? "cyclist" : "pedestrian",
                                 entry.childCount, entry.slots);
                } else if (entry.hasBike) {
                    Logger::info(SRC, TAG, "Boarding Tourist %u: cyclist with bike (%u slots)",
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
                    Logger::error(SRC, TAG, "Tourist %u needs %u slots (max %u) - cannot board!",
                                  entry.touristId, entry.slots, Constants::Chair::SLOTS_PER_CHAIR);
                    // Remove from queue
                    queue.removeTourist(i);
                    shm_->operational.touristsInLowerStation--;
                    sem_.post(Semaphore::Index::STATION_CAPACITY, 1, false);
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
        } else {
            sem_.post(Semaphore::Index::CHAIRS_AVAILABLE, 1, false); // Release, no passengers
        }
    }

    SharedMemory<SharedRopewayState> shm_;
    Semaphore sem_;
    MessageQueue<WorkerMessage> msgQueue_;
    MessageQueue<EntryGateRequest> entryRequestQueue_;
    MessageQueue<EntryGateResponse> entryResponseQueue_;
    bool isEmergencyStopped_;
    time_t lastDangerCheckTime_;
    bool hasDetectedDanger_;
    std::optional<EntryGateRequest> pendingEntryRequest_;
    int32_t currentEmergencyRecordIndex_{-1};
};

int main(int argc, char *argv[]) {
    ArgumentParser::WorkerArgs args{};
    if (!ArgumentParser::parseWorkerArgs(argc, argv, args)) {
        return 1;
    }

    SignalHelper::setup(g_signals, true);

    try {
        Config::loadEnvFile();
        Logger::initCentralized(args.shmKey, args.semKey, args.logMsgKey);

        LowerWorkerProcess worker(args);
        worker.run();

        Logger::cleanupCentralized();
    } catch (const std::exception &e) {
        Logger::error(SRC, TAG, "Exception: %s", e.what());
        return 1;
    }

    return 0;
}
