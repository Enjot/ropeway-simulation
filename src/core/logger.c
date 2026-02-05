#include "core/logger.h"
#include "core/time_sim.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static SharedState *g_state = NULL;
static int g_use_colors = 0;
static LogComponent g_component = LOG_UNKNOWN;
static int g_debug_enabled = 1;

#define COLOR_RESET "\033[0m"

// Color for each component type (indexed by LogComponent enum)
static const char *g_component_colors[LOG_COMPONENT_COUNT] = {
    [LOG_TOURIST]      = "\033[92m",  // bright green
    [LOG_VIP]          = "\033[91m",  // bright red
    [LOG_CASHIER]      = "\033[33m",  // yellow
    [LOG_LOWER_WORKER] = "\033[34m",  // blue
    [LOG_UPPER_WORKER] = "\033[36m",  // cyan
    [LOG_GENERATOR]    = "\033[35m",  // magenta
    [LOG_MAIN]         = "\033[37m",  // white
    [LOG_IPC]          = "\033[90m",  // gray
    [LOG_TIME_SERVER]  = "\033[93m",  // bright yellow
    [LOG_UNKNOWN]      = "\033[39m",  // default
};

/**
 * @brief Initialize the logger with shared state and component type.
 *
 * @param state Shared memory state for time synchronization.
 * @param comp Component type for colored output selection.
 */
void logger_init(SharedState *state, LogComponent comp) {
    g_state = state;
    g_component = comp;
    g_use_colors = isatty(STDERR_FILENO);
}

/**
 * @brief Enable or disable debug log output.
 *
 * @param enabled 1 to enable debug logs, 0 to disable.
 */
void logger_set_debug_enabled(int enabled) {
    g_debug_enabled = enabled;
}

/**
 * @brief Format simulation time as HH:MM:SS string.
 *
 * @param buf Output buffer for the formatted time.
 * @param buf_size Size of the output buffer.
 */
static void format_sim_time(char *buf, int buf_size) {
    double total_sim_minutes = g_state ? time_get_sim_minutes_f(g_state) : 0.0;
    int total_minutes = (int)total_sim_minutes;
    int hours = total_minutes / 60;
    int minutes = total_minutes % 60;

    // Calculate simulated seconds from fractional minutes
    int seconds = (int)((total_sim_minutes - total_minutes) * 60);

    // Clamp values
    if (hours > 23) hours = 23;
    if (hours < 0) hours = 0;
    if (seconds > 59) seconds = 59;
    if (seconds < 0) seconds = 0;

    snprintf(buf, buf_size, "%02d:%02d:%02d", hours, minutes, seconds);
}

/**
 * @brief Log a formatted message to stderr.
 *
 * NOT signal-safe (uses snprintf). Format: [HH:MM:SS] [LEVEL] [COMPONENT] message
 *
 * @param level Log level string (LOG_DEBUG, LOG_INFO, etc.).
 * @param component Component name for the log entry.
 * @param fmt Printf-style format string.
 */
void log_msg(const char *level, const char *component, const char *fmt, ...) {
    // Skip debug logs if disabled
    if (!g_debug_enabled && strcmp(level, LOG_DEBUG) == 0) {
        return;
    }

    char buf[512];
    char time_buf[16];
    int len = 0;

    // Format sim time
    format_sim_time(time_buf, sizeof(time_buf));

    // Get color for this component (based on process's component type set at init)
    // Special case: KID logs use bright green for visibility
    const char *color = "";
    const char *reset = "";
    if (g_use_colors) {
        if (strcmp(component, "KID") == 0) {
            color = "\033[32m";  // green for kids (darker than tourist's bright green)
        } else if (g_component < LOG_COMPONENT_COUNT) {
            color = g_component_colors[g_component];
        }
        reset = COLOR_RESET;
    }

    // Format prefix: [TIME] [LEVEL] [COMPONENT] (with color)
    len = snprintf(buf, sizeof(buf), "%s[%s] [%-5s] [%s] ", color, time_buf, level, component);

    // Format message
    va_list args;
    va_start(args, fmt);
    len += vsnprintf(buf + len, sizeof(buf) - len, fmt, args);
    va_end(args);

    // Add reset and newline
    if (len < (int)sizeof(buf) - (int)sizeof(COLOR_RESET)) {
        len += snprintf(buf + len, sizeof(buf) - len, "%s\n", reset);
    }

    // Write to stderr (async-signal-safe)
    write(STDERR_FILENO, buf, len);
}

/**
 * @brief Signal-safe logging for use in signal handlers only.
 *
 * Uses write() syscall directly. Only pass pre-formatted static strings.
 *
 * @param msg Message string to write to stderr.
 */
void log_signal_safe(const char *msg) {
    if (msg) {
        write(STDERR_FILENO, msg, strlen(msg));
    }
}

/**
 * @brief Signal-safe integer to string conversion.
 *
 * @param n Integer value to convert.
 * @param buf Output buffer for the string.
 * @param buf_size Size of the output buffer.
 */
void int_to_str(int n, char *buf, int buf_size) {
    if (buf_size <= 0) return;

    if (n == 0) {
        if (buf_size >= 2) {
            buf[0] = '0';
            buf[1] = '\0';
        }
        return;
    }

    char tmp[16];
    int i = 0;
    int neg = 0;

    if (n < 0) {
        neg = 1;
        n = -n;
    }

    while (n > 0 && i < 15) {
        tmp[i++] = '0' + (n % 10);
        n /= 10;
    }

    int j = 0;
    if (neg && j < buf_size - 1) {
        buf[j++] = '-';
    }

    while (i > 0 && j < buf_size - 1) {
        buf[j++] = tmp[--i];
    }

    buf[j] = '\0';
}
