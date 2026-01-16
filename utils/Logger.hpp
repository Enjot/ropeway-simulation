#pragma once

#include <unistd.h>
#include <cstring>
#include <string>
#include <type_traits>

/**
 * Logging utility with levels and colors.
 * Uses write() for async-signal-safe output.
 */
namespace Logger {

    /**
     * Log levels with associated colors
     */
    enum class Level {
        DEBUG,
        INFO,
        WARN,
        ERROR
    };

    namespace Color {
        constexpr const char* RESET   = "\033[0m";
        constexpr const char* GRAY    = "\033[90m";      // DEBUG - gray
        constexpr const char* CYAN    = "\033[36m";      // INFO - cyan
        constexpr const char* YELLOW  = "\033[33m";      // WARN - yellow
        constexpr const char* RED     = "\033[31m";      // ERROR - red
        constexpr const char* BOLD    = "\033[1m";
    }

    namespace detail {
        inline void writeOut(const char* str) {
            write(STDOUT_FILENO, str, strlen(str));
        }

        inline void writeOut(const std::string& str) {
            write(STDOUT_FILENO, str.c_str(), str.size());
        }

        template<typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
        void writeOut(T num) {
            writeOut(std::to_string(num));
        }

        inline void writeAll() {}

        template<typename T, typename... Args>
        void writeAll(const T& first, const Args&... rest) {
            writeOut(first);
            writeAll(rest...);
        }

        inline const char* levelToString(Level level) {
            switch (level) {
                case Level::DEBUG: return "DEBUG";
                case Level::INFO:  return "INFO ";
                case Level::WARN:  return "WARN ";
                case Level::ERROR: return "ERROR";
                default:           return "?????";
            }
        }

        inline const char* levelToColor(Level level) {
            switch (level) {
                case Level::DEBUG: return Color::GRAY;
                case Level::INFO:  return Color::CYAN;
                case Level::WARN:  return Color::YELLOW;
                case Level::ERROR: return Color::RED;
                default:           return Color::RESET;
            }
        }

        inline void writeLevel(Level level) {
            writeOut(levelToColor(level));
            writeOut(" [");
            writeOut(levelToString(level));
            writeOut("] ");
            writeOut(Color::RESET);
        }

        inline void writePrefix(const char* prefix, Level level) {
            writeOut(levelToColor(level));
            writeOut("[");
            writeOut(prefix);
            writeOut("]");
            writeOut(Color::RESET);
            writeOut(" ");
        }
    }

    /**
     * Main logging function with level, prefix, and message
     */
    template<typename... Args>
    void logLevel(Level level, const char* prefix, const Args&... args) {
        detail::writeLevel(level);
        detail::writePrefix(prefix, level);
        detail::writeAll(args...);
        write(STDOUT_FILENO, "\n", 1);
    }

    /**
     * Convenience functions for each log level
     */
    template<typename... Args>
    void debug(const char* prefix, const Args&... args) {
        logLevel(Level::DEBUG, prefix, args...);
    }

    template<typename... Args>
    void info(const char* prefix, const Args&... args) {
        logLevel(Level::INFO, prefix, args...);
    }

    template<typename... Args>
    void warn(const char* prefix, const Args&... args) {
        logLevel(Level::WARN, prefix, args...);
    }

    template<typename... Args>
    void error(const char* prefix, const Args&... args) {
        logLevel(Level::ERROR, prefix, args...);
    }

    /**
     * Simple log without prefix (for headers, etc.)
     */
    template<typename... Args>
    void log(const Args&... args) {
        detail::writeAll(args...);
        write(STDOUT_FILENO, "\n", 1);
    }

    /**
     * Error logging with errno
     */
    inline void perr(const char* prefix, const char* context) {
        logLevel(Level::ERROR, prefix, context, ": errno=", errno);
    }

    /**
     * State transition logging
     */
    inline void stateChange(const char* prefix, const char* oldState, const char* newState) {
        logLevel(Level::DEBUG, prefix, oldState, " -> ", newState);
    }

    /**
     * Separator line
     */
    inline void separator(char ch = '-', int count = 60) {
        std::string line(count, ch);
        line += '\n';
        write(STDOUT_FILENO, line.c_str(), line.size());
    }

}
