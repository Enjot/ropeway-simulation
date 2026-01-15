#pragma once

#include <unistd.h>
#include <cstring>
#include <string>
#include <type_traits>

/**
 * Simple logging utility using write() for output.
 * Uses variadic templates for flexible argument handling.
 */
namespace Logger {

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

        // Base case - no more arguments
        inline void writeAll() {}

        // Recursive variadic template
        template<typename T, typename... Args>
        void writeAll(const T& first, const Args&... rest) {
            writeOut(first);
            writeAll(rest...);
        }
    }

    // Main logging function - accepts any number of arguments
    template<typename... Args>
    void info(const char* prefix, const Args&... args) {
        detail::writeOut("[");
        detail::writeOut(prefix);
        detail::writeOut("] ");
        detail::writeAll(args...);
        write(STDOUT_FILENO, "\n", 1);
    }

    // Simple log without prefix
    template<typename... Args>
    void log(const Args&... args) {
        detail::writeAll(args...);
        write(STDOUT_FILENO, "\n", 1);
    }

    // Error logging with errno
    inline void perr(const char* prefix, const char* context) {
        info(prefix, context, ": errno=", errno);
    }

    // State transition
    inline void stateChange(const char* prefix, const char* oldState, const char* newState) {
        info(prefix, oldState, " -> ", newState);
    }

    // Separator line
    inline void separator(char ch = '-', int count = 60) {
        std::string line(count, ch);
        line += '\n';
        write(STDOUT_FILENO, line.c_str(), line.size());
    }

}
