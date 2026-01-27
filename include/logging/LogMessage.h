#pragma once

#include <cstdint>
#include <ctime>
#include <sys/time.h>

/**
 * Log message structure for centralized logging via message queue.
 * All processes send logs to LoggerProcess which prints them in order.
 */
struct LogMessage {
    // Ordering fields
    uint64_t sequenceNum;       // Global sequence number for ordering
    struct timeval timestamp;   // High-resolution timestamp

    // Log content
    uint8_t level;              // Log level (DEBUG=0, INFO=1, WARN=2, ERROR=3)
    char tag[32];               // Source tag (e.g., "Tourist 5", "LowerWorker")
    char text[256];             // Log message text

    LogMessage() : sequenceNum{0}, timestamp{}, level{1}, tag{}, text{} {}
};

/**
 * Log levels matching Logger::Level
 */
namespace LogLevel {
    constexpr uint8_t DEBUG = 0;
    constexpr uint8_t INFO = 1;
    constexpr uint8_t WARN = 2;
    constexpr uint8_t ERROR = 3;
}
