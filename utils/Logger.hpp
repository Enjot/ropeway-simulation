#pragma once

#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <ctime>

#include "../Config.hpp"

namespace Logger {
    enum class Level { DEBUG, INFO, WARN, ERROR };

    namespace detail {
        constexpr const char *colors[] = {"\033[90m", "\033[36m", "\033[33m", "\033[31m"};
        constexpr const char *names[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};

        // Simulation start time (set by each process to enable simulated time display)
        inline time_t simulationStartTime = 0;

        /** Calculate simulated time string (HH:MM) */
        inline void getSimulatedTime(char* buffer) {
            if (simulationStartTime == 0) {
                buffer[0] = '\0';
                return;
            }

            time_t now = time(nullptr);
            time_t elapsed = now - simulationStartTime;

            uint32_t simulatedElapsed = static_cast<uint32_t>(elapsed) * Config::Simulation::TIME_SCALE;
            uint32_t simulatedSeconds = Config::Simulation::OPENING_HOUR * 3600 + simulatedElapsed;

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
        detail::simulationStartTime = startTime;
    }

    template<typename... Args>
    void debug(const char *tag, const char *message, Args... args) {
        if constexpr (Config::Logging::IS_DEBUG_ENABLED) {
            detail::log(Level::DEBUG, tag, message, args...);
        }
    }

    template<typename... Args>
    void info(const char *tag, const char *message, Args... args) {
        if constexpr (Config::Logging::IS_INFO_ENABLED) {
            detail::log(Level::INFO, tag, message, args...);
        }
    }

    template<typename... Args>
    void warn(const char *tag, const char *message, Args... args) {
        if constexpr (Config::Logging::IS_WARN_ENABLED) {
            detail::log(Level::WARN, tag, message, args...);
        }
    }

    template<typename... Args>
    void error(const char *tag, const char *message, Args... args) {
        if constexpr (Config::Logging::IS_ERROR_ENABLED) {
            detail::log(Level::ERROR, tag, message, args...);
        }
    }

    inline void pError(const char *message) { perror(message); }

    // perror with tag (prints errno message)
    inline void perror(const char *tag, const char *message) {
        if constexpr (Config::Logging::IS_ERROR_ENABLED) {
            detail::log(Level::ERROR, tag, "%s: %s", message, strerror(errno));
        }
    }

    // State change logging
    inline void stateChange(const char *tag, const char *from, const char *to) {
        if constexpr (Config::Logging::IS_INFO_ENABLED) {
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
