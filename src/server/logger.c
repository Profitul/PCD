#include "config.h"
#include "logger.h"

static FILE *g_log_file = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Converteste nivelul de log in string pentru afisare. */
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

/* Creeaza directorul daca lipseste. Accepta doar componente normale de path. */
static int ensure_dir(const char *path) {
    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        errno = ENOTDIR;
        return -1;
    }

    if (mkdir(path, 0755) < 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

/* Creeaza directoarele parinte pentru fisierul de log, ex. logs/server.log -> logs/. */
static int ensure_parent_dirs_for_file(const char *path) {
    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    char temp[BUFFER_SIZE];
    const int rc = snprintf(temp, sizeof(temp), "%s", path);
    if (rc < 0 || (size_t)rc >= sizeof(temp)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    char *last_slash = strrchr(temp, '/');
    if (last_slash == NULL) {
        return 0;
    }

    if (last_slash == temp) {
        return 0;
    }

    *last_slash = '\0';

    for (char *p = temp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (ensure_dir(temp) < 0) {
                return -1;
            }
            *p = '/';
        }
    }

    return ensure_dir(temp);
}

/* Deschide fisierul de log in modul append. Creeaza automat directorul logs/ daca lipseste. */
int logger_init(const char *path) {
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (ensure_parent_dirs_for_file(path) < 0) {
        return -1;
    }

    g_log_file = fopen(path, "a");
    if (g_log_file == NULL) {
        return -1;
    }

    return 0;
}

/* Inchide fisierul de log (thread-safe). */
void logger_close(void) {
    (void)pthread_mutex_lock(&g_log_mutex);

    if (g_log_file != NULL) {
        (void)fclose(g_log_file);
        g_log_file = NULL;
    }

    (void)pthread_mutex_unlock(&g_log_mutex);
}

/* Scrie un mesaj formatat in log cu timestamp si nivel (thread-safe prin mutex global). */
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
