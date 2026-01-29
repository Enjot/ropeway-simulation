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
#include "core/Config.h"
#include "utils/SignalHelper.h"
#include "utils/ArgumentParser.h"
#include "logging/Logger.h"

namespace {
    SignalHelper::Flags g_signals;
    constexpr const char *TAG = "UpperWorker";
    constexpr auto SRC = Logger::Source::UpperWorker;
}

class UpperWorkerProcess {
public:
    static constexpr long MSG_TYPE_TO_LOWER = 1;
    static constexpr long MSG_TYPE_FROM_LOWER = 2;

    // Autonomous emergency detection parameters
    static constexpr uint32_t DANGER_CHECK_INTERVAL_SEC = 5;
    static constexpr double DANGER_DETECTION_CHANCE = 0.10; // 10% chance per check

    UpperWorkerProcess(const ArgumentParser::WorkerArgs &args)
        : shm_{SharedMemory<SharedRopewayState>::attach(args.shmKey)},
          sem_{args.semKey},
          msgQueue_{args.msgKey, "WorkerMsg"},
          isEmergencyStopped_{false},
          lastDangerCheckTime_{0} {
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            shm_->operational.upperWorkerPid = getpid();
            Logger::setSimulationStartTime(shm_->operational.openingTime);
        }

        Logger::info(SRC, TAG, "Started (PID: %d)", getpid());
        sem_.post(Semaphore::Index::UPPER_WORKER_READY, 1, false);
    }

    void run() {
        Logger::info(SRC, TAG, "Monitoring upper station");
        srand(static_cast<unsigned>(time(nullptr)) ^ static_cast<unsigned>(getpid()));

        // Set up periodic alarm for status logging (every 5 seconds)
        signal(SIGALRM, [](int) {});
        alarm(5);

        while (!g_signals.exit) {
            // Check for emergency signal from external source
            if (g_signals.emergency) {
                g_signals.emergency = 0;
                handleEmergencyStop();
            }

            // Check for resume signal from external source
            if (g_signals.resume && isEmergencyStopped_) {
                g_signals.resume = 0;
                handleResumeRequest();
            }

            // Autonomous danger detection (only when not already in emergency)
            if (!isEmergencyStopped_) {
                checkForDanger();
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
        Logger::warn(SRC, TAG, "Shutting down");
    }

private:
    void handleEmergencyStop() {
        Logger::warn(SRC, TAG, "!!! EMERGENCY STOP RECEIVED !!!");

        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            shm_->operational.state = RopewayState::EMERGENCY_STOP;
        }

        isEmergencyStopped_ = true;
        Logger::info(SRC, TAG, "Emergency stop acknowledged");
    }

    void handleResumeRequest() {
        Logger::info(SRC, TAG, "Resume signal received, confirming ready...");

        // Check for ready message from LowerWorker
        auto msg = msgQueue_.tryReceive(MSG_TYPE_FROM_LOWER);
        if (msg && msg->signal == WorkerSignal::READY_TO_START) {
            Logger::info(SRC, TAG, "LowerWorker ready, sending confirmation");
        }

        // Send confirmation back to LowerWorker
        sendMessage(WorkerSignal::READY_TO_START, "UpperWorker ready to resume");
        Logger::info(SRC, TAG, "Confirmation sent to LowerWorker");

        isEmergencyStopped_ = false;
    }

    void handleMessage(const WorkerMessage &msg) {
        switch (msg.signal) {
            case WorkerSignal::EMERGENCY_STOP:
                Logger::warn(SRC, TAG, "Emergency stop message from LowerWorker");
                handleEmergencyStop();
                break;

            case WorkerSignal::READY_TO_START:
                Logger::info(SRC, TAG, "LowerWorker ready to resume");
                // Confirmation will be sent when we receive SIGUSR2
                break;

            case WorkerSignal::STATION_CLEAR:
                Logger::info(SRC, TAG, "Station clear message from LowerWorker");
                break;

            case WorkerSignal::DANGER_DETECTED:
                Logger::warn(SRC, TAG, "Danger detected by LowerWorker");
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

    /**
     * Autonomous danger detection.
     * Simulates worker detecting a problem at the upper station.
     */
    void checkForDanger() {
        time_t now = time(nullptr) - shm_->operational.totalPausedSeconds;

        // Only check periodically
        if (now - lastDangerCheckTime_ < DANGER_CHECK_INTERVAL_SEC) {
            return;
        }
        lastDangerCheckTime_ = now;

        // Random chance to detect "danger"
        double roll = static_cast<double>(rand()) / RAND_MAX;
        if (roll < DANGER_DETECTION_CHANCE) {
            Logger::warn(SRC, TAG, "!!! DANGER DETECTED - Initiating emergency stop !!!");
            triggerEmergencyStop();

            // Update statistics
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_STATS);
                shm_->stats.dailyStats.emergencyStops++;
            }

            // Simulate time to assess and resolve the danger (3-6 seconds)
            int resolveTime = 3 + (rand() % 4);
            Logger::info(SRC, TAG, "Assessing danger... (estimated %d seconds)", resolveTime);

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
            initiateResume();
        }
    }

    void triggerEmergencyStop() {
        Logger::warn(SRC, TAG, "!!! EMERGENCY STOP TRIGGERED !!!");

        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            shm_->operational.state = RopewayState::EMERGENCY_STOP;
        }

        isEmergencyStopped_ = true;

        // Notify LowerWorker
        sendMessage(WorkerSignal::EMERGENCY_STOP, "Emergency stop by UpperWorker");

        // Also send signal to LowerWorker
        pid_t lowerWorkerPid;
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            lowerWorkerPid = shm_->operational.lowerWorkerPid;
        }
        if (lowerWorkerPid > 0) {
            kill(lowerWorkerPid, SIGUSR1);
        }

        Logger::info(SRC, TAG, "Emergency stop activated");
    }

    void initiateResume() {
        Logger::info(SRC, TAG, "Resume requested, checking with LowerWorker...");

        // Send ready message to LowerWorker
        sendMessage(WorkerSignal::READY_TO_START, "UpperWorker ready to resume");

        // Wake up LowerWorker with signal
        pid_t lowerWorkerPid;
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            lowerWorkerPid = shm_->operational.lowerWorkerPid;
        }
        if (lowerWorkerPid > 0) {
            kill(lowerWorkerPid, SIGUSR2);
        }

        // Wait for LowerWorker confirmation
        Logger::info(SRC, TAG, "Waiting for LowerWorker confirmation...");
        std::optional<WorkerMessage> response;
        while (!g_signals.exit) {
            response = msgQueue_.receiveInterruptible(MSG_TYPE_FROM_LOWER);
            if (response) break;
        }

        if (response && response->signal == WorkerSignal::READY_TO_START) {
            Logger::info(SRC, TAG, "LowerWorker confirmed ready. Resuming operations!");
        }

        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            if (shm_->operational.acceptingNewTourists) {
                shm_->operational.state = RopewayState::RUNNING;
            } else {
                shm_->operational.state = RopewayState::CLOSING;
            }
        }
        isEmergencyStopped_ = false;
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
                Logger::warn(SRC, TAG, "EMERGENCY STOP - Rides: %u, At upper: %u", totalRides, touristsAtUpper);
            } else {
                Logger::info(SRC, TAG, "Rides: %u | Upper: %u (bikes: %u, walking: %u)",
                            totalRides, touristsAtUpper, cyclistsExiting, pedestriansExiting);
            }
            lastLog = now;
        }
    }

    SharedMemory<SharedRopewayState> shm_;
    Semaphore sem_;
    MessageQueue<WorkerMessage> msgQueue_;
    bool isEmergencyStopped_;
    time_t lastDangerCheckTime_;
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
        Logger::error(SRC, TAG, "Exception: %s", e.what());
        return 1;
    }

    return 0;
}
