#pragma once

#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <sys/time.h>
#include <sys/types.h>
#include <memory>

#include "core/Flags.h"
#include "logging/LogMessage.h"

// Forward declarations to avoid circular includes
class Semaphore;
template<typename T>
class MessageQueue;
struct SharedRopewayState;
template<typename T>
class SharedMemory;

namespace Logger {
    enum class Level { DEBUG, INFO, WARN, ERROR };

    enum class Source : uint8_t {
        LowerWorker,
        UpperWorker,
        Cashier,
        Tourist,
        Other
    };

    namespace detail {
        constexpr const char *names[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};

        inline const char *getTagColor(Source source, Level level) {
            if (level == Level::ERROR) return "\033[31m";
            switch (source) {
                case Source::LowerWorker: return "\033[36m";
                case Source::UpperWorker: return "\033[35m";
                case Source::Cashier: return "\033[33m";
                case Source::Tourist: return "\033[32m";
                default: return "\033[37m";
            }
        }

        // Centralized logging state
        inline bool centralizedMode = false;
        inline key_t logQueueKey = -1;
        inline key_t semKey = -1;
        inline key_t shmKey = -1;
        inline MessageQueue<LogMessage> *logQueue = nullptr;
        inline Semaphore *sem = nullptr;
        inline SharedMemory<SharedRopewayState> *shm = nullptr;

        // Simulation start time with microsecond precision
        inline struct timeval simulationStartTime = {0, 0};

        /** Calculate simulated time string (HH:MM) */
        inline void getSimulatedTime(char *buffer) {
            if (simulationStartTime.tv_sec == 0) {
                buffer[0] = '\0';
                return;
            }

            struct timeval now;
            gettimeofday(&now, nullptr);

            // Calculate elapsed time in microseconds for sub-second precision
            int64_t elapsedUs = (now.tv_sec - simulationStartTime.tv_sec) * 1000000LL +
                                (now.tv_usec - simulationStartTime.tv_usec);
            if (elapsedUs < 0) elapsedUs = 0;

            // Convert to simulated seconds: elapsed_us * TIME_SCALE / 1_000_000
            // Note: Config not available here in centralized mode, use default scale
            uint32_t timeScale = 600; // Default, will be overridden
            uint32_t openingHour = 8;
            uint32_t simulatedElapsed = static_cast<uint32_t>(elapsedUs * timeScale / 1000000);
            uint32_t simulatedSeconds = openingHour * 3600 + simulatedElapsed;

            if (simulatedSeconds > 24 * 3600 - 1) {
                simulatedSeconds = 24 * 3600 - 1;
            }

            uint32_t hours = simulatedSeconds / 3600;
            uint32_t minutes = (simulatedSeconds % 3600) / 60;
            snprintf(buffer, 8, "[%02u:%02u]", hours, minutes);
        }

        // Direct logging (used when not in centralized mode or by LoggerProcess)
        template<typename... Args>
        void logDirect(const Source source, Level level, const char *tag, const char *message, Args... args) {
            char buf[512];
            char timeBuf[8] = "";
            getSimulatedTime(timeBuf);

            const char *color = getTagColor(source, level);
            int n;
            if (timeBuf[0] != '\0') {
                n = snprintf(buf, sizeof(buf), "\033[90m%s\033[0m %s[%s] [%s]\033[0m ",
                             timeBuf,
                             color,
                             names[static_cast<int>(level)],
                             tag);
            } else {
                n = snprintf(buf, sizeof(buf), "%s[%s] [%s]\033[0m ",
                             color,
                             names[static_cast<int>(level)],
                             tag);
            }
            n += snprintf(buf + n, sizeof(buf) - n, message, args...);
            buf[n++] = '\n';
            write(STDOUT_FILENO, buf, n);
        }

        void sendToQueue(Source source, Level level, const char *tag, const char *text);

        template<typename... Args>
        void log(Source source, Level level, const char *tag, const char *message, Args... args) {
            if (centralizedMode && logQueue != nullptr) {
                // Format message
                char text[256];
                snprintf(text, sizeof(text), message, args...);
                sendToQueue(source, level, tag, text);
            } else {
                logDirect(source, level, tag, message, args...);
            }
        }
    }

    /** Initialize centralized logging mode */
    void initCentralized(key_t shmKey, key_t semKey, key_t logQueueKey);

    /** Cleanup centralized logging resources */
    void cleanupCentralized();

    /** Set simulation start time to enable simulated time in logs */
    inline void setSimulationStartTime(time_t startTime) {
        struct timeval now;
        gettimeofday(&now, nullptr);
        time_t offset = now.tv_sec - startTime;
        detail::simulationStartTime.tv_sec = now.tv_sec - offset;
        detail::simulationStartTime.tv_usec = now.tv_usec;
    }

    template<typename... Args>
    void debug(Source source, const char *tag, const char *message, Args... args) {
        if constexpr (Flags::Logging::IS_DEBUG_ENABLED) {
            detail::log(source, Level::DEBUG, tag, message, args...);
        }
    }

    template<typename... Args>
    void info(Source source, const char *tag, const char *message, Args... args) {
        if constexpr (Flags::Logging::IS_INFO_ENABLED) {
            detail::log(source, Level::INFO, tag, message, args...);
        }
    }

    template<typename... Args>
    void warn(Source source, const char *tag, const char *message, Args... args) {
        if constexpr (Flags::Logging::IS_WARN_ENABLED) {
            detail::log(source, Level::WARN, tag, message, args...);
        }
    }

    template<typename... Args>
    void error(Source source, const char *tag, const char *message, Args... args) {
        if constexpr (Flags::Logging::IS_ERROR_ENABLED) {
            detail::log(source, Level::ERROR, tag, message, args...);
        }
    }

    inline void pError(const char *message) { perror(message); }

    inline void perror(Source source, const char *tag, const char *message) {
        if constexpr (Flags::Logging::IS_ERROR_ENABLED) {
            detail::log(source, Level::ERROR, tag, "%s: %s", message, strerror(errno));
        }
    }

    inline void stateChange(Source source, const char *tag, const char *from, const char *to) {
        if constexpr (Flags::Logging::IS_INFO_ENABLED) {
            detail::log(source, Level::INFO, tag, "%s -> %s", from, to);
        }
    }

    inline void separator(char ch = '-', int count = 60) {
        char buf[128];
        int n = (count < 127) ? count : 127;
        memset(buf, ch, n);
        buf[n++] = '\n';
        write(STDOUT_FILENO, buf, n);
    }
}
