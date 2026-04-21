#define _GNU_SOURCE
#include "runtime_config.h"
#include "config.h"
#include "logger.h"

#include <getopt.h>
#include <sys/utsname.h>
#include <pwd.h>

#ifdef HAVE_LIBCONFIG
#include <libconfig.h>
#endif

#define STEGA_VERSION "1.0.0"

static void copy_str(char *dst, const char *src, size_t dst_size) {
    if (src == NULL || dst_size == 0U) return;
    (void)snprintf(dst, dst_size, "%s", src);
}

void runtime_config_set_defaults(runtime_config_t *cfg) {
    (void)memset(cfg, 0, sizeof(*cfg));
    cfg->user_port = SERVER_PORT;
    cfg->admin_port = ADMIN_PORT;
    cfg->worker_count = DEFAULT_WORKER_COUNT;
    copy_str(cfg->log_path, LOG_PATH, sizeof(cfg->log_path));
    copy_str(cfg->storage_root, STORAGE_ROOT, sizeof(cfg->storage_root));
    copy_str(cfg->config_file, DEFAULT_CONFIG_PATH, sizeof(cfg->config_file));
    cfg->max_upload_bytes = MAX_UPLOAD_BYTES;
}

int runtime_config_load_file(runtime_config_t *cfg, const char *path) {
    if (path == NULL || path[0] == '\0') return -1;
#ifdef HAVE_LIBCONFIG
    config_t lc;
    config_init(&lc);
    if (config_read_file(&lc, path) != CONFIG_TRUE) {
        (void)fprintf(stderr, "libconfig: %s:%d - %s\n",
                      config_error_file(&lc), config_error_line(&lc),
                      config_error_text(&lc));
        config_destroy(&lc);
        return -1;
    }

    int ival = 0;
    const char *sval = NULL;
    long long llv = 0;

    if (config_lookup_int(&lc, "server.user_port", &ival) == CONFIG_TRUE)   cfg->user_port = ival;
    if (config_lookup_int(&lc, "server.admin_port", &ival) == CONFIG_TRUE)  cfg->admin_port = ival;
    if (config_lookup_int(&lc, "server.worker_count", &ival) == CONFIG_TRUE) {
        cfg->worker_count = (ival < 1) ? 1 : ((ival > MAX_WORKERS) ? MAX_WORKERS : ival);
    }
    if (config_lookup_string(&lc, "server.log_path", &sval) == CONFIG_TRUE && sval != NULL)
        copy_str(cfg->log_path, sval, sizeof(cfg->log_path));
    if (config_lookup_string(&lc, "server.storage_root", &sval) == CONFIG_TRUE && sval != NULL)
        copy_str(cfg->storage_root, sval, sizeof(cfg->storage_root));
    if (config_lookup_int64(&lc, "server.max_upload_mb", &llv) == CONFIG_TRUE && llv > 0)
        cfg->max_upload_bytes = (uint64_t)llv * 1024ULL * 1024ULL;

    copy_str(cfg->config_file, path, sizeof(cfg->config_file));
    config_destroy(&lc);
    return 0;
#else
    (void)cfg;
    (void)fprintf(stderr,
        "warning: server compiled without HAVE_LIBCONFIG; ignoring %s\n", path);
    return -1;
#endif
}

static int parse_int_env(const char *name, int *out) {
    const char *v = getenv(name);
    if (v == NULL || v[0] == '\0') return 0;
    char *end = NULL;
    const long parsed = strtol(v, &end, 10);
    if (end == v || *end != '\0') return 0;
    *out = (int)parsed;
    return 1;
}

void runtime_config_apply_env(runtime_config_t *cfg) {
    int iv = 0;
    if (parse_int_env("STEGA_USER_PORT", &iv))  cfg->user_port = iv;
    if (parse_int_env("STEGA_ADMIN_PORT", &iv)) cfg->admin_port = iv;
    if (parse_int_env("STEGA_WORKERS", &iv)) {
        cfg->worker_count = (iv < 1) ? 1 : ((iv > MAX_WORKERS) ? MAX_WORKERS : iv);
    }
    const char *sv = getenv("STEGA_LOG");
    if (sv != NULL && sv[0] != '\0') copy_str(cfg->log_path, sv, sizeof(cfg->log_path));
    sv = getenv("STEGA_STORAGE");
    if (sv != NULL && sv[0] != '\0') copy_str(cfg->storage_root, sv, sizeof(cfg->storage_root));
}

void runtime_config_print_help(const char *prog) {
    (void)printf(
        "StegaPNG server " STEGA_VERSION "\n"
        "Usage: %s [options]\n"
        "Options:\n"
        "  -c, --config FILE       Path to libconfig file (default: %s)\n"
        "  -p, --port N            User TCP port (default: %d)\n"
        "  -a, --admin-port N      Admin TCP port (default: %d)\n"
        "  -w, --workers N         Worker thread count 1..%d (default: %d)\n"
        "  -l, --log FILE          Log file path (default: %s)\n"
        "  -s, --storage DIR       Storage root (default: %s)\n"
        "  -h, --help              Show this help and exit\n"
        "  -V, --version           Print version and exit\n"
        "\nEnvironment variables (override config file, overridden by CLI):\n"
        "  STEGA_USER_PORT  STEGA_ADMIN_PORT  STEGA_WORKERS\n"
        "  STEGA_LOG        STEGA_STORAGE     STEGA_CONFIG\n",
        prog, DEFAULT_CONFIG_PATH, SERVER_PORT, ADMIN_PORT,
        MAX_WORKERS, DEFAULT_WORKER_COUNT, LOG_PATH, STORAGE_ROOT);
}

void runtime_config_print_version(void) {
    (void)printf("stegapng-server " STEGA_VERSION "\n");
#ifdef HAVE_LIBCONFIG
    (void)printf("features: libconfig=on\n");
#else
    (void)printf("features: libconfig=off\n");
#endif
}

int runtime_config_apply_cli(runtime_config_t *cfg, int argc, char *argv[], int *should_exit) {
    *should_exit = 0;
    static const struct option long_opts[] = {
        {"config",     required_argument, NULL, 'c'},
        {"port",       required_argument, NULL, 'p'},
        {"admin-port", required_argument, NULL, 'a'},
        {"workers",    required_argument, NULL, 'w'},
        {"log",        required_argument, NULL, 'l'},
        {"storage",    required_argument, NULL, 's'},
        {"help",       no_argument,       NULL, 'h'},
        {"version",    no_argument,       NULL, 'V'},
        {NULL, 0, NULL, 0}
    };
    optind = 1;
    int opt = 0;
    while ((opt = getopt_long(argc, argv, "c:p:a:w:l:s:hV", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'c': copy_str(cfg->config_file, optarg, sizeof(cfg->config_file)); break;
            case 'p': cfg->user_port = atoi(optarg); break;
            case 'a': cfg->admin_port = atoi(optarg); break;
            case 'w': {
                const int w = atoi(optarg);
                cfg->worker_count = (w < 1) ? 1 : ((w > MAX_WORKERS) ? MAX_WORKERS : w);
                break;
            }
            case 'l': copy_str(cfg->log_path, optarg, sizeof(cfg->log_path)); break;
            case 's': copy_str(cfg->storage_root, optarg, sizeof(cfg->storage_root)); break;
            case 'h': runtime_config_print_help(argv[0]);    *should_exit = 1; return 0;
            case 'V': runtime_config_print_version();        *should_exit = 1; return 0;
            default:
                (void)fprintf(stderr, "Try '%s --help' for usage.\n", argv[0]);
                *should_exit = 1;
                return -1;
        }
    }
    return 0;
}

void runtime_config_log_summary(const runtime_config_t *cfg) {
    logger_log(LOG_LEVEL_INFO,
        "Config: user_port=%d admin_port=%d workers=%d log=%s storage=%s max_upload=%lluMB src=%s",
        cfg->user_port, cfg->admin_port, cfg->worker_count,
        cfg->log_path, cfg->storage_root,
        (unsigned long long)(cfg->max_upload_bytes / (1024ULL * 1024ULL)),
        cfg->config_file);
}

void runtime_config_log_environment(void) {
    struct utsname un;
    if (uname(&un) == 0) {
        logger_log(LOG_LEVEL_INFO, "Env: sys=%s node=%s rel=%s mach=%s",
                   un.sysname, un.nodename, un.release, un.machine);
    }
    const uid_t uid = getuid();
    const pid_t pid = getpid();
    const char *user = NULL;
    struct passwd *pw = getpwuid(uid);
    if (pw != NULL) user = pw->pw_name;
    logger_log(LOG_LEVEL_INFO, "Env: pid=%d uid=%d user=%s",
               (int)pid, (int)uid, (user != NULL) ? user : "?");
    const char *path = getenv("PATH");
    const char *lang = getenv("LANG");
    logger_log(LOG_LEVEL_INFO, "Env: PATH_len=%zu LANG=%s",
               (path != NULL) ? strlen(path) : 0U,
               (lang != NULL) ? lang : "(unset)");
}
