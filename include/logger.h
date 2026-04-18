#ifndef LOGGER_H
#define LOGGER_H

#include "common.h"

typedef enum log_level {
    LOG_LEVEL_INFO = 0,
    LOG_LEVEL_WARN = 1,
    LOG_LEVEL_ERROR = 2
} log_level_t;

int logger_init(const char *path);
void logger_close(void);
void logger_log(log_level_t level, const char *fmt, ...);

#endif