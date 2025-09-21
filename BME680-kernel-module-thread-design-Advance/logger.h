#ifndef LOGGER_H
#define LOGGER_H

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
} log_level_t;

int logger_init(const char *filename);
void logger_destroy(void);
void logger_set_level(log_level_t level);
void logger_log(log_level_t level, const char *format, ...);

#endif /* LOGGER_H */