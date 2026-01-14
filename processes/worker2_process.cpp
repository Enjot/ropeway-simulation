#include <iostream>
#include <cstring>
#include <unistd.h>
#include <ctime>
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

namespace {
    SignalHelper::SignalFlags g_signals;
}

class Worker2Process {
public:
    static constexpr uint32_t WORKER_ID = 2;
    static constexpr long MSG_TYPE_TO_WORKER1 = 1;
    static constexpr long MSG_TYPE_FROM_WORKER1 = 2;

    Worker2Process(const ArgumentParser::WorkerArgs& args)
        : shm_{args.shmKey, false},
          sem_{args.semKey, SemaphoreIndex::TOTAL_SEMAPHORES, false},
          msgQueue_{args.msgKey, false},
          isEmergencyStopped_{false},
          exitRoute1Active_{true},
          exitRoute2Active_{true},
          currentEmergencyRecordIndex_{-1} {

        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            shm_->worker2Pid = getpid();
        }

        std::cout << "[Worker2] Started (PID: " << getpid() << ") - Upper Station Controller" << std::endl;
    }

    void run() {
        std::cout << "[Worker2] Beginning operations" << std::endl;
        std::cout << "[Worker2] Managing " << Config::Gate::NUM_EXIT_ROUTES << " exit routes" << std::endl;

        while (!SignalHelper::shouldExit(g_signals)) {
            if (SignalHelper::isEmergency(g_signals)) {
                handleEmergencyReceived();
                SignalHelper::clearFlag(g_signals.emergency);
            }

            if (g_signals.resume) {
                handleLocalEmergencyTrigger();
                SignalHelper::clearFlag(g_signals.resume);
            }

            checkMessages();

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

            usleep(50000);
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
        sendMessage(WorkerSignal::EMERGENCY_STOP, "Emergency stop initiated by Worker2");

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
                if (isStationClear()) {
                    sendMessage(WorkerSignal::READY_TO_START, "Worker2 confirms ready to resume");
                    isEmergencyStopped_ = false;
                    currentEmergencyRecordIndex_ = -1;
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
        return true;
    }

    void handleRunningState() {
        uint32_t chairsInUse;
        uint32_t totalRides;
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            chairsInUse = shm_->chairsInUse;
            totalRides = shm_->totalRidesToday;
        }

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
    }

    void handleStoppedState() {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            std::cout << "[Worker2] Ropeway stopped. Closing exit routes." << std::endl;
            exitRoute1Active_ = false;
            exitRoute2Active_ = false;
            loggedOnce = true;
        }
        usleep(500000);
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
    ArgumentParser::WorkerArgs args{};
    if (!ArgumentParser::parseWorkerArgs(argc, argv, args)) {
        return 1;
    }

    SignalHelper::setup(g_signals, SignalHelper::Mode::WORKER);

    try {
        Worker2Process worker(args);
        worker.run();
    } catch (const std::exception& e) {
        std::cerr << "[Worker2] Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}