#pragma once

#include <unistd.h>
#include <cstdio>
#include <cstring>

#include "../Config.hpp"

namespace Logger {
    enum class Level { DEBUG, INFO, WARN, ERROR };

    namespace detail {
        constexpr const char *colors[] = {"\033[90m", "\033[36m", "\033[33m", "\033[31m"};
        constexpr const char *names[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};

        template<typename... Args>
        void log(Level level, const char *tag, const char *message, Args... args) {
            char buf[512];
            int n = snprintf(buf, sizeof(buf), "%s[%s] [%s]\033[0m ",
                             colors[static_cast<int>(level)],
                             names[static_cast<int>(level)],
                             tag);
            n += snprintf(buf + n, sizeof(buf) - n, message, args...);
            buf[n++] = '\n';
            write(STDOUT_FILENO, buf, n);
        }
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

    inline void separator(char ch = '-', int count = 60) {
        char buf[128];
        int n = (count < 127) ? count : 127;
        memset(buf, ch, n);
        buf[n++] = '\n';
        write(STDOUT_FILENO, buf, n);
    }
}
