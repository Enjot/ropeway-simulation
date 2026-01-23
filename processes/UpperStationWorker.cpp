#include <cstring>
#include <unistd.h>
#include <ctime>
#include <csignal>

#include "ipc/SharedMemory.hpp"
#include "../ipc/core/Semaphore.hpp"
#include "../ipc/core/MessageQueue.hpp"
#include "ipc/RopewaySystemState.hpp"
#include "../ipc/message/WorkerMessage.hpp"
#include "../Config.hpp"
#include "utils/SignalHelper.hpp"
#include "utils/ArgumentParser.hpp"
#include "utils/Logger.hpp"

namespace {
    SignalHelper::SignalFlags g_signals;
    constexpr const char *TAG = "Worker2";
}

class UpperWorkerProcess {
public:
    static constexpr uint32_t WORKER_ID = 2;
    static constexpr long MSG_TYPE_TO_WORKER1 = 1;
    static constexpr long MSG_TYPE_FROM_WORKER1 = 2;

    UpperWorkerProcess(const ArgumentParser::WorkerArgs &args)
        : shm_{args.shmKey, false},
          sem_{args.semKey},
          msgQueue_{args.msgKey, false},
          isEmergencyStopped_{false},
          exitRoute1Active_{true},
          exitRoute2Active_{true},
          currentEmergencyRecordIndex_{-1} {
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
            shm_->core.worker2Pid = getpid();
        }

        Logger::info(TAG, "Started (PID: ", getpid(), ") - Upper Station Controller");

        // Signal readiness to parent process
        sem_.post(Semaphore::Index::UPPER_WORKER_READY, false);
    }

    void run() {
        Logger::info(TAG, "Beginning operations");
        Logger::info(TAG, "Managing ", Config::Gate::NUM_EXIT_ROUTES, " exit routes");

        while (!SignalHelper::shouldExit(g_signals)) {
            if (SignalHelper::isEmergency(g_signals)) {
                handleEmergencyReceived();
                SignalHelper::clearFlag(g_signals.emergency);
            }

            if (g_signals.resume) {
                // Resume signal received - check messages for resume request from Worker1
                SignalHelper::clearFlag(g_signals.resume);
            }

            // Check for messages from Worker1 (non-blocking)
            auto msg = msgQueue_.tryReceive(MSG_TYPE_FROM_WORKER1);
            if (msg) {
                handleMessage(*msg);
            }

            RopewayState currentState = RopewayState::RUNNING; // Default if lock fails
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
                currentState = shm_->core.state;
            }

            switch (currentState) {
                case RopewayState::RUNNING:
                    handleRunningState();
                    // Worker2 just waits for messages/signals - use blocking receive
                    waitForMessage();
                    break;
                case RopewayState::EMERGENCY_STOP:
                    handleEmergencyState();
                    // Wait for signal using pause() - will wake up on SIGUSR2 from Worker1
                    pause();
                    break;
                case RopewayState::CLOSING:
                    handleClosingState();
                    waitForMessage();
                    break;
                case RopewayState::STOPPED:
                    handleStoppedState();
                    break;
            }
        }

        Logger::info(TAG, "Shutting down");
    }

private:
    void waitForMessage() {
        // Try to receive message - will be interrupted by signals
        auto msg = msgQueue_.receive(MSG_TYPE_FROM_WORKER1);
        if (msg) {
            handleMessage(*msg);
        }
        // If no message (interrupted), just continue loop
    }

    void handleEmergencyReceived() {
        Logger::info(TAG, "!!! EMERGENCY STOP RECEIVED FROM WORKER1 !!!");

        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
            shm_->core.state = RopewayState::EMERGENCY_STOP;
        }

        isEmergencyStopped_ = true;
        sendMessage(WorkerSignal::EMERGENCY_STOP, "Emergency stop acknowledged by Worker2");
    }

    void handleLocalEmergencyTrigger() {
        Logger::info(TAG, "!!! LOCAL EMERGENCY TRIGGERED !!!");

        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
            shm_->core.state = RopewayState::EMERGENCY_STOP;
            currentEmergencyRecordIndex_ = shm_->stats.dailyStats.recordEmergencyStart(WORKER_ID);
        }

        isEmergencyStopped_ = true;
        sendMessage(WorkerSignal::EMERGENCY_STOP, "Emergency stop initiated by Worker2");

        pid_t worker1Pid;
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
            worker1Pid = shm_->core.worker1Pid;
        }
        if (worker1Pid > 0) {
            kill(worker1Pid, SIGUSR1);
        }

        Logger::info(TAG, "Emergency stop signal sent to Worker1");
    }

    void handleMessage(const WorkerMessage &msg) {
        Logger::info(TAG, "Received message from Worker1");

        switch (msg.signal) {
            case WorkerSignal::EMERGENCY_STOP:
                Logger::info(TAG, "Worker1 triggered emergency stop");
                {
                    Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
                    shm_->core.state = RopewayState::EMERGENCY_STOP;
                }
                isEmergencyStopped_ = true;
                break;

            case WorkerSignal::READY_TO_START:
                Logger::info(TAG, "Worker1 requests resume - confirming ready");
                if (isStationClear()) {
                    sendMessage(WorkerSignal::READY_TO_START, "Worker2 confirms ready to resume");
                    isEmergencyStopped_ = false;
                    currentEmergencyRecordIndex_ = -1;
                } else {
                    sendMessage(WorkerSignal::DANGER_DETECTED, "Worker2 station not clear");
                }
                break;

            case WorkerSignal::STATION_CLEAR:
                Logger::info(TAG, "Worker1 reports station clear");
                break;

            case WorkerSignal::DANGER_DETECTED:
                Logger::info(TAG, "Worker1 detected danger - initiating emergency stop");
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
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
            chairsInUse = shm_->chairPool.chairsInUse;
            totalRides = shm_->core.totalRidesToday;
        }

        static time_t lastStatusLog = 0;
        time_t now = time(nullptr);
        if (now - lastStatusLog >= 5) {
            Logger::info(TAG, "Status: ChairsInUse=", chairsInUse,
                         ", TotalRidesToday=", totalRides);
            lastStatusLog = now;
        }
    }

    void handleEmergencyState() {
        static time_t lastLog = 0;
        time_t now = time(nullptr);
        if (now - lastLog >= 2) {
            Logger::info(TAG, "EMERGENCY STOP active - awaiting clearance from Worker1");
            lastLog = now;
        }
    }

    void handleClosingState() {
        static time_t lastLog = 0;
        time_t now = time(nullptr);
        if (now - lastLog >= 2) {
            Logger::info(TAG, "Closing sequence - monitoring exit routes");
            lastLog = now;
        }
    }

    void handleStoppedState() {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            Logger::info(TAG, "Ropeway stopped. Closing exit routes.");
            exitRoute1Active_ = false;
            exitRoute2Active_ = false;
            loggedOnce = true;
        }
        // Wait for termination signal
        pause();
    }

    void sendMessage(WorkerSignal signal, const char *text) {
        WorkerMessage msg;
        msg.mtype = MSG_TYPE_TO_WORKER1;
        msg.senderId = WORKER_ID;
        msg.receiverId = 1;
        msg.signal = signal;
        msg.timestamp = time(nullptr);
        std::strncpy(msg.messageText, text, sizeof(msg.messageText) - 1);
        msg.messageText[sizeof(msg.messageText) - 1] = '\0';

        if (!msgQueue_.send(msg)) {
            Logger::perror(TAG, "msgsnd to Worker1");
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

int main(int argc, char *argv[]) {
    ArgumentParser::WorkerArgs args{};
    if (!ArgumentParser::parseWorkerArgs(argc, argv, args)) {
        return 1;
    }

    SignalHelper::setup(g_signals, SignalHelper::Mode::WORKER);

    try {
        UpperWorkerProcess worker(args);
        worker.run();
    } catch (const std::exception &e) {
        Logger::perror(TAG, e.what());
        return 1;
    }

    return 0;
}
