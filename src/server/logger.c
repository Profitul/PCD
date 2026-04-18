#include "logger.h"

static FILE *g_log_file = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *level_to_string(const log_level_t level) {
    switch (level) {
        case LOG_LEVEL_INFO:
            return "INFO";
        case LOG_LEVEL_WARN:
            return "WARN";
        case LOG_LEVEL_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

int logger_init(const char *path) {
    if (path == NULL) {
        return -1;
    }

    g_log_file = fopen(path, "a");
    if (g_log_file == NULL) {
        return -1;
    }

    return 0;
}

void logger_close(void) {
    (void)pthread_mutex_lock(&g_log_mutex);

    if (g_log_file != NULL) {
        fclose(g_log_file);
        g_log_file = NULL;
    }

    (void)pthread_mutex_unlock(&g_log_mutex);
}

void logger_log(const log_level_t level, const char *fmt, ...) {
    if (fmt == NULL) {
        return;
    }

    (void)pthread_mutex_lock(&g_log_mutex);

    FILE *out = (g_log_file != NULL) ? g_log_file : stderr;

    time_t now = time(NULL);
    struct tm tm_now;
    if (localtime_r(&now, &tm_now) == NULL) {
        (void)pthread_mutex_unlock(&g_log_mutex);
        return;
    }

    char time_buffer[64];
    if (strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &tm_now) == 0U) {
        (void)pthread_mutex_unlock(&g_log_mutex);
        return;
    }

    (void)fprintf(out, "[%s] [%s] ", time_buffer, level_to_string(level));

    va_list args;
    va_start(args, fmt);
    (void)vfprintf(out, fmt, args);
    va_end(args);

    (void)fprintf(out, "\n");
    (void)fflush(out);

    (void)pthread_mutex_unlock(&g_log_mutex);
}