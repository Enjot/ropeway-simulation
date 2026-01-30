#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

/* Log levels */
#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3

/* Current log level - can be set via environment */
static inline int get_log_level(void) {
    const char *level = getenv("ROPEWAY_LOG_LEVEL");
    if (!level) return LOG_LEVEL_INFO;
    if (level[0] == 'D' || level[0] == 'd') return LOG_LEVEL_DEBUG;
    if (level[0] == 'W' || level[0] == 'w') return LOG_LEVEL_WARN;
    if (level[0] == 'E' || level[0] == 'e') return LOG_LEVEL_ERROR;
    return LOG_LEVEL_INFO;
}

/* Get timestamp string */
static inline void get_timestamp(char *buf, size_t len) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm_info = localtime(&tv.tv_sec);
    snprintf(buf, len, "%02d:%02d:%02d.%03ld",
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
             tv.tv_usec / 1000);
}

/* Log macros - write to stderr for immediate output */
#define LOG_DEBUG(fmt, ...) do { \
    if (get_log_level() <= LOG_LEVEL_DEBUG) { \
        char _ts[16]; get_timestamp(_ts, sizeof(_ts)); \
        fprintf(stderr, "[%s] DEBUG [%d] " fmt "\n", _ts, getpid(), ##__VA_ARGS__); \
    } \
} while(0)

#define LOG_INFO(fmt, ...) do { \
    if (get_log_level() <= LOG_LEVEL_INFO) { \
        char _ts[16]; get_timestamp(_ts, sizeof(_ts)); \
        fprintf(stderr, "[%s] INFO  [%d] " fmt "\n", _ts, getpid(), ##__VA_ARGS__); \
    } \
} while(0)

#define LOG_WARN(fmt, ...) do { \
    if (get_log_level() <= LOG_LEVEL_WARN) { \
        char _ts[16]; get_timestamp(_ts, sizeof(_ts)); \
        fprintf(stderr, "[%s] WARN  [%d] " fmt "\n", _ts, getpid(), ##__VA_ARGS__); \
    } \
} while(0)

#define LOG_ERROR(fmt, ...) do { \
    char _ts[16]; get_timestamp(_ts, sizeof(_ts)); \
    fprintf(stderr, "[%s] ERROR [%d] " fmt "\n", _ts, getpid(), ##__VA_ARGS__); \
} while(0)

#endif /* LOGGER_H */
