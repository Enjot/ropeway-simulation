#include <cstring>
#include <unistd.h>
#include <ctime>
#include <vector>
#include <csignal>

#include "ipc/SharedMemory.hpp"
#include "ipc/Semaphore.hpp"
#include "ipc/MessageQueue.hpp"
#include "ipc/ropeway_system_state.hpp"
#include "ipc/worker_message.hpp"
#include "ipc/semaphore_index.hpp"
#include "common/config.hpp"
#include "utils/SignalHelper.hpp"
#include "utils/EnumStrings.hpp"
#include "utils/ArgumentParser.hpp"
#include "utils/Logger.hpp"

namespace {
    SignalHelper::SignalFlags g_signals;
    constexpr const char* TAG = "Worker1";
}

class Worker1Process {
public:
    static constexpr uint32_t WORKER_ID = 1;
    static constexpr long MSG_TYPE_TO_WORKER2 = 2;
    static constexpr long MSG_TYPE_FROM_WORKER2 = 1;

    Worker1Process(const ArgumentParser::WorkerArgs& args)
        : shm_{args.shmKey, false},
          sem_{args.semKey, SemaphoreIndex::TOTAL_SEMAPHORES, false},
          msgQueue_{args.msgKey, false},
          isEmergencyStopped_{false},
          currentEmergencyRecordIndex_{-1} {

        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            shm_->core.worker1Pid = getpid();
        }

        Logger::info(TAG, "Started (PID: ", getpid(), ") - Lower Station Controller");

        // Signal readiness to parent process
        sem_.signal(SemaphoreIndex::WORKER1_READY);
    }

    void run() {
        Logger::info(TAG, "Beginning operations");

        while (!SignalHelper::shouldExit(g_signals)) {
            if (SignalHelper::isEmergency(g_signals)) {
                handleEmergencyStopTrigger();
                SignalHelper::clearFlag(g_signals.emergency);
            }

            if (SignalHelper::isResumeRequested(g_signals) && isEmergencyStopped_) {
                handleResumeRequest();
                SignalHelper::clearFlag(g_signals.resume);
            }

            checkMessages();

            RopewayState currentState;
            {
                SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                currentState = shm_->core.state;
            }

            switch (currentState) {
                case RopewayState::RUNNING:
                    handleRunningState();
                    break;
                case RopewayState::EMERGENCY_STOP:
                    handleEmergencyState();
                    break;
                case RopewayState::CLOSING:
                    handleClosingState();
                    break;
                case RopewayState::STOPPED:
                    handleStoppedState();
                    break;
            }

            usleep(Config::Timing::WORKER_LOOP_POLL_US);
        }

        Logger::info(TAG, "Shutting down");
    }

private:
    void handleEmergencyStopTrigger() {
        Logger::info(TAG, "!!! EMERGENCY STOP TRIGGERED !!!");

        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            shm_->core.state = RopewayState::EMERGENCY_STOP;
            currentEmergencyRecordIndex_ = shm_->stats.dailyStats.recordEmergencyStart(WORKER_ID);
        }

        isEmergencyStopped_ = true;
        sendMessage(WorkerSignal::EMERGENCY_STOP, "Emergency stop initiated by Worker1");

        pid_t worker2Pid;
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            worker2Pid = shm_->core.worker2Pid;
        }
        if (worker2Pid > 0) {
            kill(worker2Pid, SIGUSR1);
        }

        Logger::info(TAG, "Emergency stop signal sent to Worker2");
    }

    void handleResumeRequest() {
        Logger::info(TAG, "Resume requested, checking with Worker2...");
        sendMessage(WorkerSignal::READY_TO_START, "Worker1 ready to resume, awaiting confirmation");

        time_t startTime = time(nullptr);
        constexpr int TIMEOUT_S = 10;

        while (time(nullptr) - startTime < TIMEOUT_S && !SignalHelper::shouldExit(g_signals)) {
            auto msg = msgQueue_.tryReceive(MSG_TYPE_FROM_WORKER2);
            if (msg && msg->signal == WorkerSignal::READY_TO_START) {
                Logger::info(TAG, "Worker2 confirmed ready. Resuming operations.");

                {
                    SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                    shm_->core.state = RopewayState::RUNNING;
                    if (currentEmergencyRecordIndex_ >= 0) {
                        shm_->stats.dailyStats.recordEmergencyEnd(currentEmergencyRecordIndex_);
                    }
                }

                isEmergencyStopped_ = false;
                currentEmergencyRecordIndex_ = -1;
                return;
            }
            usleep(Config::Timing::TOURIST_LOOP_POLL_US);
        }

        Logger::info(TAG, "Timeout waiting for Worker2 confirmation");
    }

    void checkMessages() {
        auto msg = msgQueue_.tryReceive(MSG_TYPE_FROM_WORKER2);
        if (msg) {
            handleMessage(*msg);
        }
    }

    void handleMessage(const WorkerMessage& msg) {
        Logger::info(TAG, "Received message from Worker2");

        switch (msg.signal) {
            case WorkerSignal::EMERGENCY_STOP:
                Logger::info(TAG, "Worker2 triggered emergency stop");
                {
                    SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                    shm_->core.state = RopewayState::EMERGENCY_STOP;
                }
                isEmergencyStopped_ = true;
                break;

            case WorkerSignal::STATION_CLEAR:
                Logger::info(TAG, "Worker2 reports station clear");
                break;

            case WorkerSignal::READY_TO_START:
                Logger::info(TAG, "Worker2 is ready to resume");
                break;

            case WorkerSignal::DANGER_DETECTED:
                Logger::info(TAG, "Worker2 detected danger!");
                handleEmergencyStopTrigger();
                break;
        }
    }

    void handleRunningState() {
        processBoardingQueue();

        uint32_t touristsOnPlatform;
        uint32_t chairsInUse;
        uint32_t queueCount;
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            touristsOnPlatform = shm_->core.touristsOnPlatform;
            chairsInUse = shm_->chairPool.chairsInUse;
            queueCount = shm_->chairPool.boardingQueue.count;
        }

        static time_t lastStatusLog = 0;
        time_t now = time(nullptr);
        if (now - lastStatusLog >= 5) {
            Logger::info(TAG, "Status: Platform=", touristsOnPlatform,
                        ", ChairsInUse=", chairsInUse, "/", Config::Chair::MAX_CONCURRENT_IN_USE,
                        ", Queue=", queueCount);
            lastStatusLog = now;
        }

        time_t closingTime;
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            closingTime = shm_->core.closingTime;
        }

        if (now >= closingTime) {
            Logger::info(TAG, "Closing time reached, initiating closing sequence");
            {
                SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                shm_->core.state = RopewayState::CLOSING;
                shm_->core.acceptingNewTourists = false;
            }
            sendMessage(WorkerSignal::STATION_CLEAR, "Closing time reached");
        }
    }

    void processBoardingQueue() {
        SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);

        BoardingQueue& queue = shm_->chairPool.boardingQueue;
        if (queue.count == 0) return;

        pairChildrenWithAdults(queue);
        assignTouristsToChairs(queue);
    }

    void pairChildrenWithAdults(BoardingQueue& queue) {
        for (uint32_t i = 0; i < queue.count; ++i) {
            BoardingQueueEntry& child = queue.entries[i];

            if (!child.needsSupervision || child.guardianId != -1) {
                continue;
            }

            for (uint32_t j = 0; j < queue.count; ++j) {
                if (i == j) continue;

                BoardingQueueEntry& adult = queue.entries[j];

                if (adult.isAdult && adult.dependentCount < Config::Gate::MAX_CHILDREN_PER_ADULT) {
                    child.guardianId = static_cast<int32_t>(adult.touristId);
                    adult.dependentCount++;

                    Logger::info(TAG, "Paired child ", child.touristId,
                                " (age ", child.age, ") with guardian ", adult.touristId);
                    break;
                }
            }
        }
    }

    void assignTouristsToChairs(BoardingQueue& queue) {
        if (shm_->chairPool.chairsInUse >= Config::Chair::MAX_CONCURRENT_IN_USE) {
            return;
        }

        uint32_t slotsUsed = 0;
        uint32_t cyclistCount = 0;
        std::vector<uint32_t> groupIndices;

        for (uint32_t i = 0; i < queue.count; ++i) {
            BoardingQueueEntry& entry = queue.entries[i];

            if (entry.readyToBoard || entry.assignedChairId >= 0) {
                continue;
            }

            if (entry.needsSupervision && entry.guardianId == -1) {
                continue;
            }

            uint32_t slotCost = (entry.type == TouristType::CYCLIST)
                ? Config::Chair::CYCLIST_SLOT_COST
                : Config::Chair::PEDESTRIAN_SLOT_COST;

            if (slotsUsed + slotCost > Config::Chair::SLOTS_PER_CHAIR) {
                continue;
            }

            if (entry.type == TouristType::CYCLIST) {
                if (cyclistCount >= Config::Chair::MAX_CYCLISTS_PER_CHAIR) {
                    continue;
                }
                cyclistCount++;
            }

            if (entry.needsSupervision && entry.guardianId != -1) {
                bool guardianInGroup = false;
                for (uint32_t idx : groupIndices) {
                    if (queue.entries[idx].touristId == static_cast<uint32_t>(entry.guardianId)) {
                        guardianInGroup = true;
                        break;
                    }
                }
                if (!guardianInGroup) {
                    int32_t guardianIdx = queue.findTourist(static_cast<uint32_t>(entry.guardianId));
                    if (guardianIdx >= 0) {
                        BoardingQueueEntry& guardian = queue.entries[guardianIdx];
                        uint32_t guardianSlotCost = (guardian.type == TouristType::CYCLIST)
                            ? Config::Chair::CYCLIST_SLOT_COST
                            : Config::Chair::PEDESTRIAN_SLOT_COST;

                        if (slotsUsed + guardianSlotCost + slotCost <= Config::Chair::SLOTS_PER_CHAIR) {
                            groupIndices.push_back(static_cast<uint32_t>(guardianIdx));
                            slotsUsed += guardianSlotCost;
                            if (guardian.type == TouristType::CYCLIST) {
                                cyclistCount++;
                            }
                        } else {
                            continue;
                        }
                    } else {
                        continue;
                    }
                }
            }

            groupIndices.push_back(i);
            slotsUsed += slotCost;

            if (slotsUsed >= Config::Chair::SLOTS_PER_CHAIR) {
                break;
            }
        }

        if (!groupIndices.empty()) {
            int32_t chairId = -1;
            for (uint32_t c = 0; c < Config::Chair::QUANTITY; ++c) {
                uint32_t idx = (queue.nextChairId + c) % Config::Chair::QUANTITY;
                if (!shm_->chairPool.chairs[idx].isOccupied) {
                    chairId = static_cast<int32_t>(idx);
                    queue.nextChairId = (idx + 1) % Config::Chair::QUANTITY;
                    break;
                }
            }

            if (chairId >= 0) {
                shm_->chairPool.chairs[chairId].isOccupied = true;
                shm_->chairPool.chairs[chairId].numPassengers = static_cast<uint32_t>(groupIndices.size());
                shm_->chairPool.chairs[chairId].slotsUsed = slotsUsed;
                shm_->chairPool.chairs[chairId].departureTime = time(nullptr);
                shm_->chairPool.chairs[chairId].arrivalTime = shm_->chairPool.chairs[chairId].departureTime + Config::Chair::RIDE_TIME_S;
                shm_->chairPool.chairsInUse++;

                for (size_t i = 0; i < groupIndices.size(); ++i) {
                    BoardingQueueEntry& entry = queue.entries[groupIndices[i]];
                    entry.assignedChairId = chairId;
                    entry.readyToBoard = true;

                    if (i < 4) {
                        shm_->chairPool.chairs[chairId].passengerIds[i] = static_cast<int32_t>(entry.touristId);
                    }
                }

                Logger::info(TAG, "Chair ", static_cast<unsigned int>(chairId),
                            " assigned to group of ", static_cast<unsigned int>(groupIndices.size()),
                            " tourists (slots: ", slotsUsed, ")");
            }
        }
    }

    void handleEmergencyState() {
        static time_t lastLog = 0;
        time_t now = time(nullptr);
        if (now - lastLog >= 2) {
            Logger::info(TAG, "EMERGENCY STOP active - waiting for resume signal (SIGUSR2)");
            lastLog = now;
        }
    }

    void handleClosingState() {
        uint32_t touristsOnPlatform;
        uint32_t touristsInStation;
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            touristsOnPlatform = shm_->core.touristsOnPlatform;
            touristsInStation = shm_->core.touristsInLowerStation;
        }

        if (touristsOnPlatform == 0 && touristsInStation == 0) {
            Logger::info(TAG, "All tourists cleared. Stopping ropeway in ", Config::Ropeway::SHUTDOWN_DELAY_S, " seconds...");
            sleep(Config::Ropeway::SHUTDOWN_DELAY_S);

            {
                SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                shm_->core.state = RopewayState::STOPPED;
            }

            sendMessage(WorkerSignal::STATION_CLEAR, "Ropeway stopped for the day");
        } else {
            static time_t lastLog = 0;
            time_t now = time(nullptr);
            if (now - lastLog >= 2) {
                Logger::info(TAG, "Closing: waiting for ", touristsInStation,
                            " in station, ", touristsOnPlatform, " on platform");
                lastLog = now;
            }
        }
    }

    void handleStoppedState() {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            Logger::info(TAG, "Ropeway stopped. End of operations.");
            loggedOnce = true;
        }
        usleep(Config::Timing::STOPPED_STATE_IDLE_US);
    }

    void sendMessage(WorkerSignal signal, const char* text) {
        WorkerMessage msg;
        msg.mtype = MSG_TYPE_TO_WORKER2;
        msg.senderId = WORKER_ID;
        msg.receiverId = 2;
        msg.signal = signal;
        msg.timestamp = time(nullptr);
        std::strncpy(msg.messageText, text, sizeof(msg.messageText) - 1);
        msg.messageText[sizeof(msg.messageText) - 1] = '\0';

        if (!msgQueue_.send(msg)) {
            Logger::perr(TAG, "msgsnd to Worker2");
        }
    }

    SharedMemory<RopewaySystemState> shm_;
    Semaphore sem_;
    MessageQueue<WorkerMessage> msgQueue_;
    bool isEmergencyStopped_;
    int32_t currentEmergencyRecordIndex_;
};

int main(int argc, char* argv[]) {
    ArgumentParser::WorkerArgs args{};
    if (!ArgumentParser::parseWorkerArgs(argc, argv, args)) {
        return 1;
    }

    SignalHelper::setup(g_signals, SignalHelper::Mode::WORKER);

    try {
        Worker1Process worker(args);
        worker.run();
    } catch (const std::exception& e) {
        Logger::perr(TAG, e.what());
        return 1;
    }

    return 0;
}
