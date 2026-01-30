#pragma once

#include "types.h"

// Log levels
#define LOG_DEBUG "DEBUG"
#define LOG_INFO  "INFO"
#define LOG_WARN  "WARN"
#define LOG_ERROR "ERROR"

// Initialize logger with shared state (for sim time)
void logger_init(SharedState *state);

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
