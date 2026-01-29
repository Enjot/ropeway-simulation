#include <cstring>
#include <unistd.h>
#include <ctime>
#include <csignal>

#include "ipc/core/SharedMemory.h"
#include "ipc/core/Semaphore.h"
#include "ipc/core/MessageQueue.h"
#include "ipc/model/SharedRopewayState.h"
#include "ropeway/worker/WorkerMessage.h"
#include "core/Config.h"
#include "utils/SignalHelper.h"
#include "utils/ArgumentParser.h"
#include "logging/Logger.h"

namespace {
    SignalHelper::Flags g_signals;
    constexpr const char *TAG = "UpperWorker";
}

class UpperWorkerProcess {
public:
    static constexpr long MSG_TYPE_TO_LOWER = 1;
    static constexpr long MSG_TYPE_FROM_LOWER = 2;

    UpperWorkerProcess(const ArgumentParser::WorkerArgs &args)
        : shm_{SharedMemory<SharedRopewayState>::attach(args.shmKey)},
          sem_{args.semKey},
          msgQueue_{args.msgKey, "WorkerMsg"},
          isEmergencyStopped_{false} {
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            shm_->operational.upperWorkerPid = getpid();
            Logger::setSimulationStartTime(shm_->operational.openingTime);
        }

        Logger::info(TAG, "Started (PID: %d)", getpid());
        sem_.post(Semaphore::Index::UPPER_WORKER_READY, 1, false);
    }

    void run() {
        Logger::info(TAG, "Monitoring upper station");

        // Set up periodic alarm for status logging (every 5 seconds)
        signal(SIGALRM, [](int) {});
        alarm(5);

        while (!g_signals.exit) {
            // Check for emergency signal
            if (g_signals.emergency) {
                g_signals.emergency = 0;
                handleEmergencyStop();
            }

            // Check for resume signal
            if (g_signals.resume && isEmergencyStopped_) {
                g_signals.resume = 0;
                handleResumeRequest();
            }

            // Block on message receive (interrupted by signals)
            auto msg = msgQueue_.receiveInterruptible(MSG_TYPE_FROM_LOWER);
            if (msg) {
                handleMessage(*msg);
            }

            // Log status and reset alarm
            logStatus();
            alarm(5);
        }

        alarm(0);
        Logger::warn(TAG, "Shutting down");
    }

private:
    void handleEmergencyStop() {
        Logger::warn(TAG, "!!! EMERGENCY STOP RECEIVED !!!");

        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            shm_->operational.state = RopewayState::EMERGENCY_STOP;
        }

        isEmergencyStopped_ = true;
        Logger::info(TAG, "Emergency stop acknowledged");
    }

    void handleResumeRequest() {
        Logger::info(TAG, "Resume signal received, confirming ready...");

        // Check for ready message from LowerWorker
        auto msg = msgQueue_.tryReceive(MSG_TYPE_FROM_LOWER);
        if (msg && msg->signal == WorkerSignal::READY_TO_START) {
            Logger::info(TAG, "LowerWorker ready, sending confirmation");
        }

        // Send confirmation back to LowerWorker
        sendMessage(WorkerSignal::READY_TO_START, "UpperWorker ready to resume");
        Logger::info(TAG, "Confirmation sent to LowerWorker");

        isEmergencyStopped_ = false;
    }

    void handleMessage(const WorkerMessage &msg) {
        switch (msg.signal) {
            case WorkerSignal::EMERGENCY_STOP:
                Logger::warn(TAG, "Emergency stop message from LowerWorker");
                handleEmergencyStop();
                break;

            case WorkerSignal::READY_TO_START:
                Logger::info(TAG, "LowerWorker ready to resume");
                // Confirmation will be sent when we receive SIGUSR2
                break;

            case WorkerSignal::STATION_CLEAR:
                Logger::info(TAG, "Station clear message from LowerWorker");
                break;

            case WorkerSignal::DANGER_DETECTED:
                Logger::warn(TAG, "Danger detected by LowerWorker");
                handleEmergencyStop();
                break;
        }
    }

    void sendMessage(WorkerSignal signal, const char *text) {
        WorkerMessage msg;
        msg.senderId = 2;
        msg.receiverId = 1;
        msg.signal = signal;
        msg.timestamp = time(nullptr);
        strncpy(msg.messageText, text, sizeof(msg.messageText) - 1);
        msg.messageText[sizeof(msg.messageText) - 1] = '\0';

        msgQueue_.send(msg, MSG_TYPE_TO_LOWER);
    }

    void logStatus() {
        static time_t lastLog = 0;
        time_t now = time(nullptr) - shm_->operational.totalPausedSeconds;
        if (now - lastLog >= 5) {
            uint32_t totalRides;
            uint32_t touristsAtUpper;
            uint32_t cyclistsExiting;
            uint32_t pedestriansExiting;
            RopewayState state;
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
                totalRides = shm_->operational.totalRidesToday;
                touristsAtUpper = shm_->operational.touristsAtUpperStation;
                cyclistsExiting = shm_->operational.cyclistsOnBikeTrailExit;
                pedestriansExiting = shm_->operational.pedestriansOnWalkingExit;
                state = shm_->operational.state;
            }

            if (state == RopewayState::EMERGENCY_STOP) {
                Logger::warn(TAG, "EMERGENCY STOP - Rides: %u, At upper: %u", totalRides, touristsAtUpper);
            } else {
                Logger::info(TAG, "Rides: %u | Upper: %u (bikes: %u, walking: %u)",
                            totalRides, touristsAtUpper, cyclistsExiting, pedestriansExiting);
            }
            lastLog = now;
        }
    }

    SharedMemory<SharedRopewayState> shm_;
    Semaphore sem_;
    MessageQueue<WorkerMessage> msgQueue_;
    bool isEmergencyStopped_;
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

        UpperWorkerProcess worker(args);
        worker.run();

        Logger::cleanupCentralized();
    } catch (const std::exception &e) {
        Logger::error(TAG, "Exception: %s", e.what());
        return 1;
    }

    return 0;
}
