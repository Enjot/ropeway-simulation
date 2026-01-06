#include <iostream>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <ctime>

#include "ipc/SharedMemory.hpp"
#include "ipc/Semaphore.hpp"
#include "ipc/MessageQueue.hpp"
#include "ipc/ropeway_system_state.hpp"
#include "ipc/worker_message.hpp"
#include "ipc/semaphore_index.hpp"
#include "common/config.hpp"

namespace {
    volatile sig_atomic_t g_emergencyReceived = 0;
    volatile sig_atomic_t g_shouldExit = 0;
    volatile sig_atomic_t g_triggerEmergency = 0;

    void signalHandler(int signum) {
        if (signum == SIGUSR1) {
            g_emergencyReceived = 1;
        } else if (signum == SIGUSR2) {
            g_triggerEmergency = 1;
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

class Worker2Process {
public:
    static constexpr uint32_t WORKER_ID = 2;
    static constexpr long MSG_TYPE_TO_WORKER1 = 1;
    static constexpr long MSG_TYPE_FROM_WORKER1 = 2;

    Worker2Process(const WorkerArgs& args)
        : shm_{args.shmKey, false},
          sem_{args.semKey, SemaphoreIndex::TOTAL_SEMAPHORES, false},
          msgQueue_{args.msgKey, false},
          isEmergencyStopped_{false},
          exitRoute1Active_{true},
          exitRoute2Active_{true},
          currentEmergencyRecordIndex_{-1} {

        // Register this worker's PID
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            shm_->worker2Pid = getpid();
        }

        std::cout << "[Worker2] Started (PID: " << getpid() << ") - Upper Station Controller" << std::endl;
    }

    void run() {
        std::cout << "[Worker2] Beginning operations" << std::endl;
        std::cout << "[Worker2] Managing " << Config::Gate::NUM_EXIT_ROUTES << " exit routes" << std::endl;

        while (!g_shouldExit) {
            // Check for emergency signal from Worker1
            if (g_emergencyReceived) {
                handleEmergencyReceived();
                g_emergencyReceived = 0;
            }

            // Check for local emergency trigger
            if (g_triggerEmergency) {
                handleLocalEmergencyTrigger();
                g_triggerEmergency = 0;
            }

            // Check for messages from Worker1
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

        std::cout << "[Worker2] Shutting down" << std::endl;
    }

private:
    void handleEmergencyReceived() {
        std::cout << "[Worker2] !!! EMERGENCY STOP RECEIVED FROM WORKER1 !!!" << std::endl;

        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            shm_->state = RopewayState::EMERGENCY_STOP;
        }

        isEmergencyStopped_ = true;

        // Acknowledge to Worker1
        sendMessage(WorkerSignal::EMERGENCY_STOP, "Emergency stop acknowledged by Worker2");
    }

    void handleLocalEmergencyTrigger() {
        std::cout << "[Worker2] !!! LOCAL EMERGENCY TRIGGERED !!!" << std::endl;

        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            shm_->state = RopewayState::EMERGENCY_STOP;
            currentEmergencyRecordIndex_ = shm_->dailyStats.recordEmergencyStart(WORKER_ID);
        }

        isEmergencyStopped_ = true;

        // Notify Worker1
        sendMessage(WorkerSignal::EMERGENCY_STOP, "Emergency stop initiated by Worker2");

        // Send SIGUSR1 to Worker1
        pid_t worker1Pid;
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            worker1Pid = shm_->worker1Pid;
        }
        if (worker1Pid > 0) {
            kill(worker1Pid, SIGUSR1);
        }

        std::cout << "[Worker2] Emergency stop signal sent to Worker1" << std::endl;
    }

    void checkMessages() {
        auto msg = msgQueue_.tryReceive(MSG_TYPE_FROM_WORKER1);
        if (msg) {
            handleMessage(*msg);
        }
    }

    void handleMessage(const WorkerMessage& msg) {
        std::cout << "[Worker2] Received message from Worker1: " << msg.messageText << std::endl;

        switch (msg.signal) {
            case WorkerSignal::EMERGENCY_STOP:
                std::cout << "[Worker2] Worker1 triggered emergency stop" << std::endl;
                {
                    SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                    shm_->state = RopewayState::EMERGENCY_STOP;
                }
                isEmergencyStopped_ = true;
                break;

            case WorkerSignal::READY_TO_START:
                std::cout << "[Worker2] Worker1 requests resume - confirming ready" << std::endl;
                // Check if safe to resume
                if (isStationClear()) {
                    sendMessage(WorkerSignal::READY_TO_START, "Worker2 confirms ready to resume");
                    isEmergencyStopped_ = false;
                    currentEmergencyRecordIndex_ = -1;  // Clear our local tracking
                } else {
                    sendMessage(WorkerSignal::DANGER_DETECTED, "Worker2 station not clear");
                }
                break;

            case WorkerSignal::STATION_CLEAR:
                std::cout << "[Worker2] Worker1 reports station clear" << std::endl;
                break;

            case WorkerSignal::DANGER_DETECTED:
                std::cout << "[Worker2] Worker1 detected danger - initiating emergency stop" << std::endl;
                handleEmergencyReceived();
                break;
        }
    }

    bool isStationClear() {
        // In a real implementation, would check actual safety conditions
        return true;
    }

    void handleRunningState() {
        // Monitor upper station and manage exit routes
        uint32_t chairsInUse;
        uint32_t totalRides;
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            chairsInUse = shm_->chairsInUse;
            totalRides = shm_->totalRidesToday;
        }

        // Log periodic status
        static time_t lastStatusLog = 0;
        time_t now = time(nullptr);
        if (now - lastStatusLog >= 5) {
            std::cout << "[Worker2] Status: ChairsInUse=" << chairsInUse
                      << ", TotalRidesToday=" << totalRides
                      << ", ExitRoutes=[" << (exitRoute1Active_ ? "1:ON" : "1:OFF")
                      << ", " << (exitRoute2Active_ ? "2:ON" : "2:OFF") << "]"
                      << std::endl;
            lastStatusLog = now;
        }
    }

    void handleEmergencyState() {
        static time_t lastLog = 0;
        time_t now = time(nullptr);
        if (now - lastLog >= 2) {
            std::cout << "[Worker2] EMERGENCY STOP active - awaiting clearance from Worker1" << std::endl;
            lastLog = now;
        }
    }

    void handleClosingState() {
        static time_t lastLog = 0;
        time_t now = time(nullptr);
        if (now - lastLog >= 2) {
            std::cout << "[Worker2] Closing sequence - monitoring exit routes" << std::endl;
            lastLog = now;
        }

        // Keep exit routes open until everyone has left
    }

    void handleStoppedState() {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            std::cout << "[Worker2] Ropeway stopped. Closing exit routes." << std::endl;
            exitRoute1Active_ = false;
            exitRoute2Active_ = false;
            loggedOnce = true;
        }
        usleep(500000); // 500ms
    }

    void sendMessage(WorkerSignal signal, const char* text) {
        WorkerMessage msg;
        msg.mtype = MSG_TYPE_TO_WORKER1;
        msg.senderId = WORKER_ID;
        msg.receiverId = 1;
        msg.signal = signal;
        msg.timestamp = time(nullptr);
        std::strncpy(msg.messageText, text, sizeof(msg.messageText) - 1);
        msg.messageText[sizeof(msg.messageText) - 1] = '\0';

        if (!msgQueue_.send(msg)) {
            std::cerr << "[Worker2] Failed to send message to Worker1" << std::endl;
        }
    }

    SharedMemory<RopewaySystemState> shm_;
    Semaphore sem_;
    MessageQueue<WorkerMessage> msgQueue_;
    bool isEmergencyStopped_;
    bool exitRoute1Active_;
    bool exitRoute2Active_;
    int32_t currentEmergencyRecordIndex_;
};

int main(int argc, char* argv[]) {
    WorkerArgs args{};
    if (!parseArgs(argc, argv, args)) {
        return 1;
    }

    setupSignalHandlers();

    try {
        Worker2Process worker(args);
        worker.run();
    } catch (const std::exception& e) {
        std::cerr << "[Worker2] Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
