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

/**
 * @brief Centralized and decentralized logging system.
 *
 * Provides logging with support for both direct output and centralized
 * logging through a message queue to a dedicated logger process.
 * Logs include simulated time, color-coded sources, and log levels.
 */
namespace Logger {
    /**
     * @brief Log severity levels.
     */
    enum class Level {
        DEBUG, ///< Detailed technical information
        INFO,  ///< Business logic events
        WARN,  ///< Warning conditions
        ERROR  ///< Error conditions
    };

    /**
     * @brief Log message source identifiers.
     */
    enum class Source : uint8_t {
        LowerWorker, ///< Lower station controller
        UpperWorker, ///< Upper station controller
        Cashier,     ///< Ticket sales process
        Tourist,     ///< Tourist process
        Other        ///< Main orchestrator and utilities
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
            if constexpr (sizeof...(args) == 0) {
                n += snprintf(buf + n, sizeof(buf) - n, "%s", message);
            } else {
                n += snprintf(buf + n, sizeof(buf) - n, message, args...);
            }
            buf[n++] = '\n';
            write(STDOUT_FILENO, buf, n);
        }

        void sendToQueue(Source source, Level level, const char *tag, const char *text);

        template<typename... Args>
        void log(Source source, Level level, const char *tag, const char *message, Args... args) {
            if (centralizedMode && logQueue != nullptr) {
                // Format message
                char text[256];
                if constexpr (sizeof...(args) == 0) {
                    snprintf(text, sizeof(text), "%s", message);
                } else {
                    snprintf(text, sizeof(text), message, args...);
                }
                sendToQueue(source, level, tag, text);
            } else {
                logDirect(source, level, tag, message, args...);
            }
        }
    }

    /**
     * @brief Initialize centralized logging mode.
     * @param shmKey Shared memory key for accessing state
     * @param semKey Semaphore key for synchronization
     * @param logQueueKey Message queue key for log messages
     *
     * After calling this, all log messages are sent to the logger process
     * via message queue instead of being printed directly.
     */
    void initCentralized(key_t shmKey, key_t semKey, key_t logQueueKey);

    /**
     * @brief Cleanup centralized logging resources.
     *
     * Switches back to direct logging mode and releases IPC resources.
     */
    void cleanupCentralized();

    /**
     * @brief Set simulation start time for log timestamps.
     * @param startTime Real time when simulation started
     *
     * Enables simulated time display in log messages (e.g., [08:15]).
     */
    inline void setSimulationStartTime(time_t startTime) {
        struct timeval now;
        gettimeofday(&now, nullptr);
        time_t offset = now.tv_sec - startTime;
        detail::simulationStartTime.tv_sec = now.tv_sec - offset;
        detail::simulationStartTime.tv_usec = now.tv_usec;
    }

    /**
     * @brief Log a debug message.
     * @param source Source process identifier
     * @param tag Short identifier (e.g., "Tourist 5")
     * @param message Format string (printf-style)
     * @param args Format arguments
     *
     * Debug messages are for technical details, disabled by default in production.
     */
    template<typename... Args>
    void debug(Source source, const char *tag, const char *message, Args... args) {
        if constexpr (Flags::Logging::IS_DEBUG_ENABLED) {
            detail::log(source, Level::DEBUG, tag, message, args...);
        }
    }

    /**
     * @brief Log an info message.
     * @param source Source process identifier
     * @param tag Short identifier (e.g., "Tourist 5")
     * @param message Format string (printf-style)
     * @param args Format arguments
     *
     * Info messages are for business logic events.
     */
    template<typename... Args>
    void info(Source source, const char *tag, const char *message, Args... args) {
        if constexpr (Flags::Logging::IS_INFO_ENABLED) {
            detail::log(source, Level::INFO, tag, message, args...);
        }
    }

    /**
     * @brief Log a warning message.
     * @param source Source process identifier
     * @param tag Short identifier (e.g., "Tourist 5")
     * @param message Format string (printf-style)
     * @param args Format arguments
     *
     * Warning messages indicate potential issues.
     */
    template<typename... Args>
    void warn(Source source, const char *tag, const char *message, Args... args) {
        if constexpr (Flags::Logging::IS_WARN_ENABLED) {
            detail::log(source, Level::WARN, tag, message, args...);
        }
    }

    /**
     * @brief Log an error message.
     * @param source Source process identifier
     * @param tag Short identifier (e.g., "Tourist 5")
     * @param message Format string (printf-style)
     * @param args Format arguments
     *
     * Error messages indicate failures that need attention.
     */
    template<typename... Args>
    void error(Source source, const char *tag, const char *message, Args... args) {
        if constexpr (Flags::Logging::IS_ERROR_ENABLED) {
            detail::log(source, Level::ERROR, tag, message, args...);
        }
    }

    /**
     * @brief Print POSIX error using perror().
     * @param message Context message to prepend
     */
    inline void pError(const char *message) { perror(message); }

    /**
     * @brief Log a POSIX error with errno description.
     * @param source Source process identifier
     * @param tag Short identifier
     * @param message Context message
     *
     * Appends strerror(errno) to the message.
     */
    inline void perror(Source source, const char *tag, const char *message) {
        if constexpr (Flags::Logging::IS_ERROR_ENABLED) {
            detail::log(source, Level::ERROR, tag, "%s: %s", message, strerror(errno));
        }
    }

    /**
     * @brief Log a state transition.
     * @param source Source process identifier
     * @param tag Short identifier
     * @param from Previous state name
     * @param to New state name
     */
    inline void stateChange(Source source, const char *tag, const char *from, const char *to) {
        if constexpr (Flags::Logging::IS_INFO_ENABLED) {
            detail::log(source, Level::INFO, tag, "%s -> %s", from, to);
        }
    }

    /**
     * @brief Print a visual separator line.
     * @param ch Character to use for the line
     * @param count Number of characters in the line
     */
    inline void separator(char ch = '-', int count = 60) {
        char buf[128];
        int n = (count < 127) ? count : 127;
        memset(buf, ch, n);
        buf[n++] = '\n';
        write(STDOUT_FILENO, buf, n);
    }
}
