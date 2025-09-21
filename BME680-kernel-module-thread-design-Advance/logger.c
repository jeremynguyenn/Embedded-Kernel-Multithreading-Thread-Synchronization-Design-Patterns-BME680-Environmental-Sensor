#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include "logger.h"

struct logger {
    FILE *log_file;
    pthread_mutex_t mutex;
    log_level_t level;
};

static struct logger logger;

const char *log_level_str[] = {
    [LOG_DEBUG] = "DEBUG",
    [LOG_INFO] = "INFO",
    [LOG_WARNING] = "WARNING",
    [LOG_ERROR] = "ERROR"
};

int logger_init(const char *filename) {
    pthread_mutex_init(&logger.mutex, NULL);
    logger.level = LOG_INFO;
    logger.log_file = fopen(filename, "a");
    if (!logger.log_file) {
        fprintf(stderr, "Failed to open log file %s: %s\n", filename, strerror(errno));
        pthread_mutex_destroy(&logger.mutex);
        return -1;
    }
    if (fileno(logger.log_file) < 0) {
        fclose(logger.log_file);
        pthread_mutex_destroy(&logger.mutex);
        logger_log(LOG_ERROR, "Invalid file descriptor for log file");
        return -EBADF;
    }
    logger_log(LOG_INFO, "Logger initialized with file %s", filename);
    return 0;
}

void logger_destroy(void) {
    pthread_mutex_lock(&logger.mutex);
    if (logger.log_file) {
        logger_log(LOG_INFO, "Logger shutting down");
        fclose(logger.log_file);
        logger.log_file = NULL;
    }
    pthread_mutex_unlock(&logger.mutex);
    pthread_mutex_destroy(&logger.mutex);
}

void logger_set_level(log_level_t level) {
    if (level < LOG_DEBUG || level > LOG_ERROR) {
        logger_log(LOG_ERROR, "Invalid log level: %d", level);
        return;
    }
    pthread_mutex_lock(&logger.mutex);
    logger.level = level;
    logger_log(LOG_INFO, "Log level set to %s", log_level_str[level]);
    pthread_mutex_unlock(&logger.mutex);
}

void logger_log(log_level_t level, const char *format, ...) {
    if (level < logger.level) return;

    pthread_mutex_lock(&logger.mutex);
    if (!logger.log_file) {
        pthread_mutex_unlock(&logger.mutex);
        fprintf(stderr, "Log file not initialized\n");
        return;
    }

    char timestamp[32];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    va_list args;
    va_start(args, format);
    fprintf(logger.log_file, "[%s] [%s] ", timestamp, log_level_str[level]);
    vfprintf(logger.log_file, format, args);
    fprintf(logger.log_file, "\n");
    if (fflush(logger.log_file) != 0 || ferror(logger.log_file)) {
        pthread_mutex_unlock(&logger.mutex);
        fprintf(stderr, "Failed to write to log file: %s\n", strerror(errno));
        va_end(args);
        return;
    }
    va_end(args);
    pthread_mutex_unlock(&logger.mutex);
}