#pragma once

#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <sys/time.h>

#include "core/Config.h"
#include "core/Flags.h"

namespace Logger {
    enum class Level { DEBUG, INFO, WARN, ERROR };

    namespace detail {
        constexpr const char *colors[] = {"\033[90m", "\033[36m", "\033[33m", "\033[31m"};
        constexpr const char *names[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};

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
            uint32_t simulatedElapsed = static_cast<uint32_t>(elapsedUs * Config::Simulation::TIME_SCALE() / 1000000);
            uint32_t simulatedSeconds = Config::Simulation::OPENING_HOUR() * 3600 + simulatedElapsed;

            if (simulatedSeconds > 24 * 3600 - 1) {
                simulatedSeconds = 24 * 3600 - 1;
            }

            uint32_t hours = simulatedSeconds / 3600;
            uint32_t minutes = (simulatedSeconds % 3600) / 60;
            snprintf(buffer, 8, "[%02u:%02u]", hours, minutes);
        }

        template<typename... Args>
        void log(Level level, const char *tag, const char *message, Args... args) {
            char buf[512];
            char timeBuf[8] = "";
            getSimulatedTime(timeBuf);

            int n;
            if (timeBuf[0] != '\0') {
                n = snprintf(buf, sizeof(buf), "\033[90m%s\033[0m %s[%s] [%s]\033[0m ",
                             timeBuf,
                             colors[static_cast<int>(level)],
                             names[static_cast<int>(level)],
                             tag);
            } else {
                n = snprintf(buf, sizeof(buf), "%s[%s] [%s]\033[0m ",
                             colors[static_cast<int>(level)],
                             names[static_cast<int>(level)],
                             tag);
            }
            n += snprintf(buf + n, sizeof(buf) - n, message, args...);
            buf[n++] = '\n';
            write(STDOUT_FILENO, buf, n);
        }
    }

    /** Set simulation start time to enable simulated time in logs */
    inline void setSimulationStartTime(time_t startTime) {
        // Convert time_t to timeval by calculating offset from current time
        struct timeval now;
        gettimeofday(&now, nullptr);
        time_t offset = now.tv_sec - startTime;
        detail::simulationStartTime.tv_sec = now.tv_sec - offset;
        detail::simulationStartTime.tv_usec = now.tv_usec;
    }

    template<typename... Args>
    void debug(const char *tag, const char *message, Args... args) {
        if constexpr (Flags::Logging::IS_DEBUG_ENABLED) {
            detail::log(Level::DEBUG, tag, message, args...);
        }
    }

    template<typename... Args>
    void info(const char *tag, const char *message, Args... args) {
        if constexpr (Flags::Logging::IS_INFO_ENABLED) {
            detail::log(Level::INFO, tag, message, args...);
        }
    }

    template<typename... Args>
    void warn(const char *tag, const char *message, Args... args) {
        if constexpr (Flags::Logging::IS_WARN_ENABLED) {
            detail::log(Level::WARN, tag, message, args...);
        }
    }

    template<typename... Args>
    void error(const char *tag, const char *message, Args... args) {
        if constexpr (Flags::Logging::IS_ERROR_ENABLED) {
            detail::log(Level::ERROR, tag, message, args...);
        }
    }

    inline void pError(const char *message) { perror(message); }

    // perror with tag (prints errno message)
    inline void perror(const char *tag, const char *message) {
        if constexpr (Flags::Logging::IS_ERROR_ENABLED) {
            detail::log(Level::ERROR, tag, "%s: %s", message, strerror(errno));
        }
    }

    // State change logging
    inline void stateChange(const char *tag, const char *from, const char *to) {
        if constexpr (Flags::Logging::IS_INFO_ENABLED) {
            detail::log(Level::INFO, tag, "%s -> %s", from, to);
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
