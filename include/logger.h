#pragma once

#include "types.h"

// Log levels
#define LOG_DEBUG "DEBUG"
#define LOG_INFO  "INFO"
#define LOG_WARN  "WARN"
#define LOG_ERROR "ERROR"

// Component types for colored logging
typedef enum {
    LOG_TOURIST,
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

// Initialize logger with shared state and component type (for colored output)
void logger_init(SharedState *state, LogComponent comp);

// Enable or disable debug logs (default: enabled)
void logger_set_debug_enabled(int enabled);

// Main logging function (NOT signal-safe, uses snprintf)
// Format: [HH:MM:SS] [LEVEL] [COMPONENT] message
void log_msg(const char *level, const char *component, const char *fmt, ...);

// Convenience macros
#define log_debug(component, fmt, ...) log_msg(LOG_DEBUG, component, fmt, ##__VA_ARGS__)
#define log_info(component, fmt, ...)  log_msg(LOG_INFO, component, fmt, ##__VA_ARGS__)
#define log_warn(component, fmt, ...)  log_msg(LOG_WARN, component, fmt, ##__VA_ARGS__)
#define log_error(component, fmt, ...) log_msg(LOG_ERROR, component, fmt, ##__VA_ARGS__)

// Signal-safe logging (ONLY for signal handlers)
// Uses write() and pre-formatted static strings only
void log_signal_safe(const char *msg);

// Signal-safe int to string conversion
void int_to_str(int n, char *buf, int buf_size);
