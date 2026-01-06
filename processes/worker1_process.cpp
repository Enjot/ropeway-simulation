#include <iostream>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <ctime>
#include <vector>

#include "ipc/SharedMemory.hpp"
#include "ipc/Semaphore.hpp"
#include "ipc/MessageQueue.hpp"
#include "ipc/ropeway_system_state.hpp"
#include "ipc/worker_message.hpp"
#include "ipc/semaphore_index.hpp"
#include "common/config.hpp"

namespace {
    volatile sig_atomic_t g_triggerEmergency = 0;
    volatile sig_atomic_t g_shouldExit = 0;
    volatile sig_atomic_t g_resumeRequested = 0;

    void signalHandler(int signum) {
        if (signum == SIGUSR1) {
            g_triggerEmergency = 1;
        } else if (signum == SIGUSR2) {
            g_resumeRequested = 1;
        } else if (signum == SIGTERM || signum == SIGINT) {
            g_shouldExit = 1;
        }
    }

    void setupSignalHandlers() {
        struct sigaction sa{};
        sa.sa_handler = signalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        if (sigaction(SIGUSR1, &sa, nullptr) == -1) {
            perror("sigaction SIGUSR1");
        }
        if (sigaction(SIGUSR2, &sa, nullptr) == -1) {
            perror("sigaction SIGUSR2");
        }
        if (sigaction(SIGTERM, &sa, nullptr) == -1) {
            perror("sigaction SIGTERM");
        }
        if (sigaction(SIGINT, &sa, nullptr) == -1) {
            perror("sigaction SIGINT");
        }
    }

    void printUsage(const char* programName) {
        std::cerr << "Usage: " << programName << " <shmKey> <semKey> <msgKey>\n";
    }

    struct WorkerArgs {
        key_t shmKey;
        key_t semKey;
        key_t msgKey;
    };

    bool parseArgs(int argc, char* argv[], WorkerArgs& args) {
        if (argc != 4) {
            printUsage(argv[0]);
            return false;
        }

        char* endPtr = nullptr;

        args.shmKey = static_cast<key_t>(std::strtol(argv[1], &endPtr, 10));
        if (*endPtr != '\0') {
            std::cerr << "Error: Invalid shmKey\n";
            return false;
        }

        args.semKey = static_cast<key_t>(std::strtol(argv[2], &endPtr, 10));
        if (*endPtr != '\0') {
            std::cerr << "Error: Invalid semKey\n";
            return false;
        }

        args.msgKey = static_cast<key_t>(std::strtol(argv[3], &endPtr, 10));
        if (*endPtr != '\0') {
            std::cerr << "Error: Invalid msgKey\n";
            return false;
        }

        return true;
    }

    const char* stateToString(RopewayState state) {
        switch (state) {
            case RopewayState::STOPPED: return "STOPPED";
            case RopewayState::RUNNING: return "RUNNING";
            case RopewayState::EMERGENCY_STOP: return "EMERGENCY_STOP";
            case RopewayState::CLOSING: return "CLOSING";
            default: return "UNKNOWN";
        }
    }
}

class Worker1Process {
public:
    static constexpr uint32_t WORKER_ID = 1;
    static constexpr long MSG_TYPE_TO_WORKER2 = 2;
    static constexpr long MSG_TYPE_FROM_WORKER2 = 1;

    Worker1Process(const WorkerArgs& args)
        : shm_{args.shmKey, false},
          sem_{args.semKey, SemaphoreIndex::TOTAL_SEMAPHORES, false},
          msgQueue_{args.msgKey, false},
          isEmergencyStopped_{false},
          currentEmergencyRecordIndex_{-1} {

        // Register this worker's PID
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            shm_->worker1Pid = getpid();
        }

        std::cout << "[Worker1] Started (PID: " << getpid() << ") - Lower Station Controller" << std::endl;
    }

    void run() {
        std::cout << "[Worker1] Beginning operations" << std::endl;

        while (!g_shouldExit) {
            // Check for emergency stop trigger
            if (g_triggerEmergency) {
                handleEmergencyStopTrigger();
                g_triggerEmergency = 0;
            }

            // Check for resume request
            if (g_resumeRequested && isEmergencyStopped_) {
                handleResumeRequest();
                g_resumeRequested = 0;
            }

            // Check for messages from Worker2
            checkMessages();

            // Get current state
            RopewayState currentState;
            {
                SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                currentState = shm_->state;
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

            usleep(50000); // 50ms cycle
        }

        std::cout << "[Worker1] Shutting down" << std::endl;
    }

private:
    void handleEmergencyStopTrigger() {
        std::cout << "[Worker1] !!! EMERGENCY STOP TRIGGERED !!!" << std::endl;

        // Set system state to emergency stop and record in statistics
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            shm_->state = RopewayState::EMERGENCY_STOP;
            currentEmergencyRecordIndex_ = shm_->dailyStats.recordEmergencyStart(WORKER_ID);
        }

        isEmergencyStopped_ = true;

        // Notify Worker2
        sendMessage(WorkerSignal::EMERGENCY_STOP, "Emergency stop initiated by Worker1");

        // Send SIGUSR1 to Worker2
        pid_t worker2Pid;
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            worker2Pid = shm_->worker2Pid;
        }
        if (worker2Pid > 0) {
            kill(worker2Pid, SIGUSR1);
        }

        std::cout << "[Worker1] Emergency stop signal sent to Worker2" << std::endl;
    }

    void handleResumeRequest() {
        std::cout << "[Worker1] Resume requested, checking with Worker2..." << std::endl;

        // Send ready-to-resume request to Worker2
        sendMessage(WorkerSignal::READY_TO_START, "Worker1 ready to resume, awaiting confirmation");

        // Wait for confirmation from Worker2 (with timeout)
        time_t startTime = time(nullptr);
        constexpr int TIMEOUT_S = 10;

        while (time(nullptr) - startTime < TIMEOUT_S && !g_shouldExit) {
            auto msg = msgQueue_.tryReceive(MSG_TYPE_FROM_WORKER2);
            if (msg && msg->signal == WorkerSignal::READY_TO_START) {
                std::cout << "[Worker1] Worker2 confirmed ready. Resuming operations." << std::endl;

                // Resume operations and record end of emergency
                {
                    SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                    shm_->state = RopewayState::RUNNING;
                    if (currentEmergencyRecordIndex_ >= 0) {
                        shm_->dailyStats.recordEmergencyEnd(currentEmergencyRecordIndex_);
                    }
                }

                isEmergencyStopped_ = false;
                currentEmergencyRecordIndex_ = -1;
                return;
            }
            usleep(100000); // 100ms
        }

        std::cout << "[Worker1] Timeout waiting for Worker2 confirmation" << std::endl;
    }

    void checkMessages() {
        auto msg = msgQueue_.tryReceive(MSG_TYPE_FROM_WORKER2);
        if (msg) {
            handleMessage(*msg);
        }
    }

    void handleMessage(const WorkerMessage& msg) {
        std::cout << "[Worker1] Received message from Worker2: " << msg.messageText << std::endl;

        switch (msg.signal) {
            case WorkerSignal::EMERGENCY_STOP:
                std::cout << "[Worker1] Worker2 triggered emergency stop" << std::endl;
                {
                    SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                    shm_->state = RopewayState::EMERGENCY_STOP;
                    // Worker2 already recorded the emergency start, just acknowledge
                }
                isEmergencyStopped_ = true;
                break;

            case WorkerSignal::STATION_CLEAR:
                std::cout << "[Worker1] Worker2 reports station clear" << std::endl;
                break;

            case WorkerSignal::READY_TO_START:
                std::cout << "[Worker1] Worker2 is ready to resume" << std::endl;
                break;

            case WorkerSignal::DANGER_DETECTED:
                std::cout << "[Worker1] Worker2 detected danger!" << std::endl;
                handleEmergencyStopTrigger();
                break;
        }
    }

    void handleRunningState() {
        // Process boarding queue - assign tourists to chairs
        processBoardingQueue();

        // Monitor platform and control ride gates
        uint32_t touristsOnPlatform;
        uint32_t chairsInUse;
        uint32_t queueCount;
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            touristsOnPlatform = shm_->touristsOnPlatform;
            chairsInUse = shm_->chairsInUse;
            queueCount = shm_->boardingQueue.count;
        }

        // Log periodic status
        static time_t lastStatusLog = 0;
        time_t now = time(nullptr);
        if (now - lastStatusLog >= 5) {
            std::cout << "[Worker1] Status: Platform=" << touristsOnPlatform
                      << ", ChairsInUse=" << chairsInUse << "/" << Config::Chair::MAX_CONCURRENT_IN_USE
                      << ", Queue=" << queueCount
                      << std::endl;
            lastStatusLog = now;
        }

        // Check if we should start closing
        time_t closingTime;
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            closingTime = shm_->closingTime;
        }

        if (now >= closingTime) {
            std::cout << "[Worker1] Closing time reached, initiating closing sequence" << std::endl;
            {
                SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                shm_->state = RopewayState::CLOSING;
                shm_->acceptingNewTourists = false;
            }
            sendMessage(WorkerSignal::STATION_CLEAR, "Closing time reached");
        }
    }

    /**
     * Process boarding queue - pair children with adults and assign to chairs
     */
    void processBoardingQueue() {
        SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);

        BoardingQueue& queue = shm_->boardingQueue;
        if (queue.count == 0) return;

        // First pass: pair children with available adults
        pairChildrenWithAdults(queue);

        // Second pass: assign tourists to chairs
        assignTouristsToChairs(queue);
    }

    /**
     * Pair children needing supervision with available adults
     */
    void pairChildrenWithAdults(BoardingQueue& queue) {
        for (uint32_t i = 0; i < queue.count; ++i) {
            BoardingQueueEntry& child = queue.entries[i];

            // Skip if not a child needing supervision or already has guardian
            if (!child.needsSupervision || child.guardianId != -1) {
                continue;
            }

            // Find an available adult
            for (uint32_t j = 0; j < queue.count; ++j) {
                if (i == j) continue;

                BoardingQueueEntry& adult = queue.entries[j];

                // Check if can be guardian (adult, not already supervising max children)
                if (adult.isAdult && adult.dependentCount < Config::Gate::MAX_CHILDREN_PER_ADULT) {
                    // Assign guardian
                    child.guardianId = static_cast<int32_t>(adult.touristId);
                    adult.dependentCount++;

                    std::cout << "[Worker1] Paired child " << child.touristId
                              << " (age " << child.age << ") with guardian " << adult.touristId
                              << std::endl;
                    break;
                }
            }
        }
    }

    /**
     * Assign tourists to chairs based on capacity rules
     */
    void assignTouristsToChairs(BoardingQueue& queue) {
        // Check if we can allocate more chairs
        if (shm_->chairsInUse >= Config::Chair::MAX_CONCURRENT_IN_USE) {
            return;
        }

        // Find tourists ready to board (not children waiting for guardian)
        // Group them for a chair
        uint32_t slotsUsed = 0;
        uint32_t cyclistCount = 0;
        std::vector<uint32_t> groupIndices;

        for (uint32_t i = 0; i < queue.count; ++i) {
            BoardingQueueEntry& entry = queue.entries[i];

            // Skip if already assigned or waiting for boarding
            if (entry.readyToBoard || entry.assignedChairId >= 0) {
                continue;
            }

            // Skip children without guardian
            if (entry.needsSupervision && entry.guardianId == -1) {
                continue;
            }

            // Calculate slot cost
            uint32_t slotCost = (entry.type == TouristType::CYCLIST)
                ? Config::Chair::CYCLIST_SLOT_COST
                : Config::Chair::PEDESTRIAN_SLOT_COST;

            // Check capacity constraints
            if (slotsUsed + slotCost > Config::Chair::SLOTS_PER_CHAIR) {
                continue; // Can't fit
            }

            // Check cyclist limit (max 2 per chair)
            if (entry.type == TouristType::CYCLIST) {
                if (cyclistCount >= Config::Chair::MAX_CYCLISTS_PER_CHAIR) {
                    continue;
                }
                cyclistCount++;
            }

            // If child, ensure guardian is in this group
            if (entry.needsSupervision && entry.guardianId != -1) {
                // Check if guardian is already in group
                bool guardianInGroup = false;
                for (uint32_t idx : groupIndices) {
                    if (queue.entries[idx].touristId == static_cast<uint32_t>(entry.guardianId)) {
                        guardianInGroup = true;
                        break;
                    }
                }
                if (!guardianInGroup) {
                    // Try to add guardian first
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
                            continue; // Can't fit both
                        }
                    } else {
                        continue; // Guardian not in queue
                    }
                }
            }

            groupIndices.push_back(i);
            slotsUsed += slotCost;

            // If chair is full, stop adding
            if (slotsUsed >= Config::Chair::SLOTS_PER_CHAIR) {
                break;
            }
        }

        // If we have tourists to board, allocate a chair
        if (!groupIndices.empty()) {
            // Find available chair
            int32_t chairId = -1;
            for (uint32_t c = 0; c < Config::Chair::QUANTITY; ++c) {
                uint32_t idx = (queue.nextChairId + c) % Config::Chair::QUANTITY;
                if (!shm_->chairs[idx].isOccupied) {
                    chairId = static_cast<int32_t>(idx);
                    queue.nextChairId = (idx + 1) % Config::Chair::QUANTITY;
                    break;
                }
            }

            if (chairId >= 0) {
                // Mark chair as occupied
                shm_->chairs[chairId].isOccupied = true;
                shm_->chairs[chairId].numPassengers = static_cast<uint32_t>(groupIndices.size());
                shm_->chairs[chairId].slotsUsed = slotsUsed;
                shm_->chairs[chairId].departureTime = time(nullptr);
                shm_->chairs[chairId].arrivalTime = shm_->chairs[chairId].departureTime + Config::Chair::RIDE_TIME_S;
                shm_->chairsInUse++;

                std::cout << "[Worker1] Chair " << chairId << " assigned to group of "
                          << groupIndices.size() << " tourists (slots: " << slotsUsed << "): ";

                // Mark tourists as ready to board
                for (size_t i = 0; i < groupIndices.size(); ++i) {
                    BoardingQueueEntry& entry = queue.entries[groupIndices[i]];
                    entry.assignedChairId = chairId;
                    entry.readyToBoard = true;

                    if (i < 4) {
                        shm_->chairs[chairId].passengerIds[i] = static_cast<int32_t>(entry.touristId);
                    }

                    std::cout << entry.touristId;
                    if (entry.needsSupervision) std::cout << "(child)";
                    if (i < groupIndices.size() - 1) std::cout << ", ";
                }
                std::cout << std::endl;
            }
        }
    }

    void handleEmergencyState() {
        // In emergency state, just wait and log
        static time_t lastLog = 0;
        time_t now = time(nullptr);
        if (now - lastLog >= 2) {
            std::cout << "[Worker1] EMERGENCY STOP active - waiting for resume signal (SIGUSR2)" << std::endl;
            lastLog = now;
        }
    }

    void handleClosingState() {
        // Wait for all tourists to exit
        uint32_t touristsOnPlatform;
        uint32_t touristsInStation;
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            touristsOnPlatform = shm_->touristsOnPlatform;
            touristsInStation = shm_->touristsInLowerStation;
        }

        if (touristsOnPlatform == 0 && touristsInStation == 0) {
            std::cout << "[Worker1] All tourists cleared. Stopping ropeway in "
                      << Config::Ropeway::SHUTDOWN_DELAY_S << " seconds..." << std::endl;
            sleep(Config::Ropeway::SHUTDOWN_DELAY_S);

            {
                SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                shm_->state = RopewayState::STOPPED;
            }

            sendMessage(WorkerSignal::STATION_CLEAR, "Ropeway stopped for the day");
        } else {
            static time_t lastLog = 0;
            time_t now = time(nullptr);
            if (now - lastLog >= 2) {
                std::cout << "[Worker1] Closing: waiting for " << touristsInStation
                          << " in station, " << touristsOnPlatform << " on platform" << std::endl;
                lastLog = now;
            }
        }
    }

    void handleStoppedState() {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            std::cout << "[Worker1] Ropeway stopped. End of operations." << std::endl;
            loggedOnce = true;
        }
        // Could exit here or wait for restart signal
        usleep(500000); // 500ms
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
            std::cerr << "[Worker1] Failed to send message to Worker2" << std::endl;
        }
    }

    SharedMemory<RopewaySystemState> shm_;
    Semaphore sem_;
    MessageQueue<WorkerMessage> msgQueue_;
    bool isEmergencyStopped_;
    int32_t currentEmergencyRecordIndex_;
};

int main(int argc, char* argv[]) {
    WorkerArgs args{};
    if (!parseArgs(argc, argv, args)) {
        return 1;
    }

    setupSignalHandlers();

    try {
        Worker1Process worker(args);
        worker.run();
    } catch (const std::exception& e) {
        std::cerr << "[Worker1] Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
