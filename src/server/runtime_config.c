#define _GNU_SOURCE
#include "runtime_config.h"
#include "config.h"
#include "logger.h"

#include <getopt.h>
#include <pwd.h>
#include <sys/utsname.h>

#ifdef HAVE_LIBCONFIG
#include <libconfig.h>
#endif

#define STEGA_VERSION "1.0.0"

/* Copie sigura de string cu snprintf(), garanteaza null-termination. */
static void copy_str(char *dst, const char *src, const size_t dst_size) {
    if (dst == NULL || src == NULL || dst_size == 0U) {
        return;
    }

    (void)snprintf(dst, dst_size, "%s", src);
}

/* Parseaza un intreg din text. Returneaza 1 la succes, 0 la eroare. */
static int parse_int_text(const char *text, int *out) {
    if (text == NULL || out == NULL || text[0] == '\0') {
        return 0;
    }

    char *end = NULL;
    errno = 0;
    const long parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }

    *out = (int)parsed;
    return 1;
}

/* Limiteaza numarul de workeri la intervalul acceptat de server. */
static int clamp_worker_count(const int workers) {
    if (workers < 1) {
        return 1;
    }
    if (workers > MAX_WORKERS) {
        return MAX_WORKERS;
    }
    return workers;
}

/* Initializeaza configuratia cu valorile implicite din config.h. */
void runtime_config_set_defaults(runtime_config_t *cfg) {
    if (cfg == NULL) {
        return;
    }

    (void)memset(cfg, 0, sizeof(*cfg));
    cfg->user_port = SERVER_PORT;
    cfg->admin_port = ADMIN_PORT;
    cfg->worker_count = DEFAULT_WORKER_COUNT;
    cfg->max_upload_bytes = MAX_UPLOAD_BYTES;

    copy_str(cfg->log_path, LOG_PATH, sizeof(cfg->log_path));
    copy_str(cfg->storage_root, STORAGE_ROOT, sizeof(cfg->storage_root));
    copy_str(cfg->config_file, DEFAULT_CONFIG_PATH, sizeof(cfg->config_file));
    copy_str(cfg->unix_socket_path, UNIX_SOCKET_PATH, sizeof(cfg->unix_socket_path));
}

/* Incarca configuratia din fisier libconfig, daca serverul a fost compilat cu HAVE_LIBCONFIG.
   Valorile din fisier suprascriu valorile implicite. */
int runtime_config_load_file(runtime_config_t *cfg, const char *path) {
    if (cfg == NULL || path == NULL || path[0] == '\0') {
        return -1;
    }

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

    if (config_lookup_int(&lc, "server.user_port", &ival) == CONFIG_TRUE) {
        cfg->user_port = ival;
    }
    if (config_lookup_int(&lc, "server.admin_port", &ival) == CONFIG_TRUE) {
        cfg->admin_port = ival;
    }
    if (config_lookup_int(&lc, "server.worker_count", &ival) == CONFIG_TRUE) {
        cfg->worker_count = clamp_worker_count(ival);
    }
    if (config_lookup_string(&lc, "server.log_path", &sval) == CONFIG_TRUE && sval != NULL) {
        copy_str(cfg->log_path, sval, sizeof(cfg->log_path));
    }
    if (config_lookup_string(&lc, "server.storage_root", &sval) == CONFIG_TRUE && sval != NULL) {
        copy_str(cfg->storage_root, sval, sizeof(cfg->storage_root));
    }
    if (config_lookup_string(&lc, "server.unix_socket_path", &sval) == CONFIG_TRUE && sval != NULL) {
        copy_str(cfg->unix_socket_path, sval, sizeof(cfg->unix_socket_path));
    }
    if (config_lookup_int64(&lc, "server.max_upload_mb", &llv) == CONFIG_TRUE && llv > 0) {
        cfg->max_upload_bytes = (uint64_t)llv * 1024ULL * 1024ULL;
    }

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

/* Citeste o variabila de mediu ca intreg. Returneaza 1 daca valoarea a fost gasita si parsata. */
static int parse_int_env(const char *name, int *out) {
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return 0;
    }

    return parse_int_text(value, out);
}

/* Aplica variabilele de mediu STEGA_* peste configuratia curenta.
   Ordine recomandata: defaults < fisier config < env < CLI. */
void runtime_config_apply_env(runtime_config_t *cfg) {
    if (cfg == NULL) {
        return;
    }

    int iv = 0;
    if (parse_int_env("STEGA_USER_PORT", &iv)) {
        cfg->user_port = iv;
    }
    if (parse_int_env("STEGA_ADMIN_PORT", &iv)) {
        cfg->admin_port = iv;
    }
    if (parse_int_env("STEGA_WORKERS", &iv)) {
        cfg->worker_count = clamp_worker_count(iv);
    }

    const char *sv = getenv("STEGA_LOG");
    if (sv != NULL && sv[0] != '\0') {
        copy_str(cfg->log_path, sv, sizeof(cfg->log_path));
    }

    sv = getenv("STEGA_STORAGE");
    if (sv != NULL && sv[0] != '\0') {
        copy_str(cfg->storage_root, sv, sizeof(cfg->storage_root));
    }

    sv = getenv("STEGA_UNIX_SOCKET");
    if (sv != NULL && sv[0] != '\0') {
        copy_str(cfg->unix_socket_path, sv, sizeof(cfg->unix_socket_path));
    }
}

void runtime_config_print_help(const char *prog) {
    (void)printf(
        "StegaPNG server " STEGA_VERSION "\n"
        "Usage: %s [options]\n"
        "Options:\n"
        "  -c, --config FILE        Path to libconfig file (default: %s)\n"
        "  -p, --port N             User TCP/INET port (default: %d)\n"
        "  -a, --admin-port N       Admin TCP/INET port (default: %d)\n"
        "  -u, --unix-socket FILE   UNIX/LOCAL socket path (default: %s)\n"
        "  -w, --workers N          Worker thread count 1..%d (default: %d)\n"
        "  -l, --log FILE           Log file path (default: %s)\n"
        "  -s, --storage DIR        Storage root (default: %s)\n"
        "  -h, --help               Show this help and exit\n"
        "  -V, --version            Print version and exit\n"
        "\nEnvironment variables (override config file, overridden by CLI):\n"
        "  STEGA_USER_PORT  STEGA_ADMIN_PORT  STEGA_WORKERS\n"
        "  STEGA_LOG        STEGA_STORAGE     STEGA_CONFIG\n"
        "  STEGA_UNIX_SOCKET\n",
        prog, DEFAULT_CONFIG_PATH, SERVER_PORT, ADMIN_PORT, UNIX_SOCKET_PATH,
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

/* Parseaza argumentele CLI cu getopt_long si le aplica peste configuratia curenta.
   Seteaza *should_exit = 1 daca s-a cerut --help sau --version. */
int runtime_config_apply_cli(runtime_config_t *cfg, int argc, char *argv[], int *should_exit) {
    if (cfg == NULL || should_exit == NULL) {
        return -1;
    }

    *should_exit = 0;

    static const struct option long_opts[] = {
        {"config",      required_argument, NULL, 'c'},
        {"port",        required_argument, NULL, 'p'},
        {"admin-port",  required_argument, NULL, 'a'},
        {"unix-socket", required_argument, NULL, 'u'},
        {"workers",     required_argument, NULL, 'w'},
        {"log",         required_argument, NULL, 'l'},
        {"storage",     required_argument, NULL, 's'},
        {"help",        no_argument,       NULL, 'h'},
        {"version",     no_argument,       NULL, 'V'},
        {NULL, 0, NULL, 0}
    };

    optind = 1;
    int opt = 0;
    while ((opt = getopt_long(argc, argv, "c:p:a:u:w:l:s:hV", long_opts, NULL)) != -1) {
        int parsed = 0;

        switch (opt) {
            case 'c':
                copy_str(cfg->config_file, optarg, sizeof(cfg->config_file));
                break;

            case 'p':
                if (!parse_int_text(optarg, &parsed)) {
                    (void)fprintf(stderr, "invalid --port value: %s\n", optarg);
                    return -1;
                }
                cfg->user_port = parsed;
                break;

            case 'a':
                if (!parse_int_text(optarg, &parsed)) {
                    (void)fprintf(stderr, "invalid --admin-port value: %s\n", optarg);
                    return -1;
                }
                cfg->admin_port = parsed;
                break;

            case 'u':
                copy_str(cfg->unix_socket_path, optarg, sizeof(cfg->unix_socket_path));
                break;

            case 'w':
                if (!parse_int_text(optarg, &parsed)) {
                    (void)fprintf(stderr, "invalid --workers value: %s\n", optarg);
                    return -1;
                }
                cfg->worker_count = clamp_worker_count(parsed);
                break;

            case 'l':
                copy_str(cfg->log_path, optarg, sizeof(cfg->log_path));
                break;

            case 's':
                copy_str(cfg->storage_root, optarg, sizeof(cfg->storage_root));
                break;

            case 'h':
                runtime_config_print_help(argv[0]);
                *should_exit = 1;
                return 0;

            case 'V':
                runtime_config_print_version();
                *should_exit = 1;
                return 0;

            default:
                (void)fprintf(stderr, "Try '%s --help' for usage.\n", argv[0]);
                *should_exit = 1;
                return -1;
        }
    }

    return 0;
}

/* Logeaza configuratia finala dupa aplicarea tuturor surselor: defaults, file, env, CLI. */
void runtime_config_log_summary(const runtime_config_t *cfg) {
    if (cfg == NULL) {
        return;
    }

    logger_log(LOG_LEVEL_INFO,
        "Config: user_port=%d admin_port=%d unix_socket=%s workers=%d log=%s storage=%s max_upload=%lluMB src=%s",
        cfg->user_port, cfg->admin_port, cfg->unix_socket_path,
        cfg->worker_count, cfg->log_path, cfg->storage_root,
        (unsigned long long)(cfg->max_upload_bytes / (1024ULL * 1024ULL)),
        cfg->config_file);
}

/* Logeaza informatii despre mediul de executie: OS, hostname, PID, user, PATH, LANG. */
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
    if (pw != NULL) {
        user = pw->pw_name;
    }

    logger_log(LOG_LEVEL_INFO, "Env: pid=%d uid=%d user=%s",
               (int)pid, (int)uid, (user != NULL) ? user : "?");

    const char *path = getenv("PATH");
    const char *lang = getenv("LANG");
    logger_log(LOG_LEVEL_INFO, "Env: PATH_len=%zu LANG=%s",
               (path != NULL) ? strlen(path) : 0U,
               (lang != NULL) ? lang : "(unset)");
}
