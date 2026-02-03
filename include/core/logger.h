#pragma once

#include "ipc/shared_state.h"

// Log levels
#define LOG_DEBUG "DEBUG"
#define LOG_INFO  "INFO"
#define LOG_WARN  "WARN"
#define LOG_ERROR "ERROR"

// Component types for colored logging
typedef enum {
    LOG_TOURIST,
    LOG_VIP,
    LOG_CASHIER,
    LOG_LOWER_WORKER,
    LOG_UPPER_WORKER,
    LOG_GENERATOR,
    LOG_MAIN,
    LOG_IPC,
    LOG_TIME_SERVER,
    LOG_UNKNOWN,
    LOG_COMPONENT_COUNT
} LogComponent;

/**
 * @brief Initialize the logger with shared state and component type.
 *
 * @param state Shared memory state for time synchronization.
 * @param comp Component type for colored output selection.
 */
void logger_init(SharedState *state, LogComponent comp);

/**
 * @brief Enable or disable debug log output.
 *
 * @param enabled 1 to enable debug logs, 0 to disable.
 */
void logger_set_debug_enabled(int enabled);

/**
 * @brief Log a formatted message to stderr.
 *
 * NOT signal-safe (uses snprintf). Format: [HH:MM:SS] [LEVEL] [COMPONENT] message
 *
 * @param level Log level string (LOG_DEBUG, LOG_INFO, etc.).
 * @param component Component name for the log entry.
 * @param fmt Printf-style format string.
 * @param ... Format arguments.
 */
void log_msg(const char *level, const char *component, const char *fmt, ...);

// Convenience macros
#define log_debug(component, fmt, ...) log_msg(LOG_DEBUG, component, fmt, ##__VA_ARGS__)
#define log_info(component, fmt, ...)  log_msg(LOG_INFO, component, fmt, ##__VA_ARGS__)
#define log_warn(component, fmt, ...)  log_msg(LOG_WARN, component, fmt, ##__VA_ARGS__)
#define log_error(component, fmt, ...) log_msg(LOG_ERROR, component, fmt, ##__VA_ARGS__)

/**
 * @brief Signal-safe logging for use in signal handlers only.
 *
 * Uses write() syscall directly. Only pass pre-formatted static strings.
 *
 * @param msg Message string to write to stderr.
 */
void log_signal_safe(const char *msg);

/**
 * @brief Signal-safe integer to string conversion.
 *
 * @param n Integer value to convert.
 * @param buf Output buffer for the string.
 * @param buf_size Size of the output buffer.
 */
void int_to_str(int n, char *buf, int buf_size);
