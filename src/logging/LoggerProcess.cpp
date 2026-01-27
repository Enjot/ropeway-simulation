#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <vector>
#include <algorithm>

#include "ipc/core/SharedMemory.h"
#include "ipc/core/Semaphore.h"
#include "ipc/core/MessageQueue.h"
#include "ipc/model/SharedRopewayState.h"
#include "logging/LogMessage.h"
#include "core/Config.h"
#include "utils/SignalHelper.h"
#include "utils/ArgumentParser.h"

namespace {
    SignalHelper::Flags g_signals;
    constexpr const char* TAG = "Logger";

    constexpr const char* colors[] = {"\033[90m", "\033[36m", "\033[33m", "\033[31m"};
    constexpr const char* names[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
}

class LoggerProcess {
public:
    LoggerProcess(key_t shmKey, key_t semKey, key_t logMsgKey)
        : shm_{SharedMemory<SharedRopewayState>::attach(shmKey)},
          sem_{semKey},
          logQueue_{logMsgKey, "LogQueue"} {

        // Get simulation start time for time display
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            simulationStartTime_ = shm_->operational.openingTime;
        }

        // Signal readiness (reuse CASHIER_READY pattern - could add LOGGER_READY)
        fprintf(stderr, "[%s] Started (PID: %d)\n", TAG, getpid());
    }

    void run() {
        while (!g_signals.exit) {
            // Receive log messages in order (negative mtype = get lowest type first)
            // This ensures messages are printed in sequence number order
            auto msg = logQueue_.tryReceive(-LONG_MAX);
            if (!msg) {
                usleep(1000); // Brief sleep if no message
                continue;
            }

            printLog(*msg);
        }

        // Drain remaining messages before exit
        drainQueue();
    }

private:
    void printLog(const LogMessage& msg) {
        char timeBuf[16] = "";
        formatSimulatedTime(msg.timestamp, timeBuf);

        char buf[512];
        int n;
        if (timeBuf[0] != '\0') {
            n = snprintf(buf, sizeof(buf), "\033[90m%s\033[0m %s[%s] [%s]\033[0m %s\n",
                         timeBuf,
                         colors[msg.level],
                         names[msg.level],
                         msg.tag,
                         msg.text);
        } else {
            n = snprintf(buf, sizeof(buf), "%s[%s] [%s]\033[0m %s\n",
                         colors[msg.level],
                         names[msg.level],
                         msg.tag,
                         msg.text);
        }
        write(STDOUT_FILENO, buf, n);
    }

    void formatSimulatedTime(const struct timeval& timestamp, char* buffer) {
        if (simulationStartTime_ == 0) {
            buffer[0] = '\0';
            return;
        }

        int64_t elapsedUs = (timestamp.tv_sec - simulationStartTime_) * 1000000LL + timestamp.tv_usec;
        if (elapsedUs < 0) elapsedUs = 0;

        uint32_t simulatedElapsed = static_cast<uint32_t>(elapsedUs * Config::Simulation::TIME_SCALE() / 1000000);
        uint32_t simulatedSeconds = Config::Simulation::OPENING_HOUR() * 3600 + simulatedElapsed;

        if (simulatedSeconds > 24 * 3600 - 1) {
            simulatedSeconds = 24 * 3600 - 1;
        }

        uint32_t hours = simulatedSeconds / 3600;
        uint32_t minutes = (simulatedSeconds % 3600) / 60;
        snprintf(buffer, 16, "[%02u:%02u]", hours, minutes);
    }

    void drainQueue() {
        std::vector<LogMessage> remaining;

        // Collect remaining messages
        while (true) {
            auto msg = logQueue_.tryReceive(0);
            if (!msg) break;
            remaining.push_back(*msg);
        }

        // Sort by sequence number
        std::sort(remaining.begin(), remaining.end(),
                  [](const LogMessage& a, const LogMessage& b) {
                      return a.sequenceNum < b.sequenceNum;
                  });

        // Print in order
        for (const auto& msg : remaining) {
            printLog(msg);
        }
    }

    SharedMemory<SharedRopewayState> shm_;
    Semaphore sem_;
    MessageQueue<LogMessage> logQueue_;
    time_t simulationStartTime_{0};
};

int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <shm_key> <sem_key> <log_msg_key>\n", argv[0]);
        return 1;
    }

    key_t shmKey = static_cast<key_t>(std::stol(argv[1]));
    key_t semKey = static_cast<key_t>(std::stol(argv[2]));
    key_t logMsgKey = static_cast<key_t>(std::stol(argv[3]));

    SignalHelper::setup(g_signals, true);

    try {
        Config::loadEnvFile();
        LoggerProcess logger(shmKey, semKey, logMsgKey);
        logger.run();
    } catch (const std::exception& e) {
        fprintf(stderr, "[%s] Exception: %s\n", TAG, e.what());
        return 1;
    }

    return 0;
}
