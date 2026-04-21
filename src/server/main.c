#include "common.h"
#include "config.h"
#include "logger.h"
#include "runtime_config.h"
#include "server.h"

/* Cauta -c/--config in argv inainte de parsarea completa getopt,
   pentru a putea incarca fisierul de config inainte ca celelalte optiuni sa fie aplicate.
   Fallback la variabila de mediu STEGA_CONFIG daca nu exista argument CLI. */
static void preparse_config_path(int argc, char *argv[], char *out, size_t out_size) {
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            (void)snprintf(out, out_size, "%s", argv[i + 1]);
            return;
        }
    }
    const char *env_cfg = getenv("STEGA_CONFIG");
    if (env_cfg != NULL && env_cfg[0] != '\0') {
        (void)snprintf(out, out_size, "%s", env_cfg);
    }
}

/* Entry point: incarca configuratia in ordinea prioritatii (defaults < file < env < CLI),
   initializeaza loggerul si porneste serverul. */
int main(int argc, char *argv[]) {
    runtime_config_t cfg;
    runtime_config_set_defaults(&cfg);

    char cfg_path[CFG_PATH_MAX];
    (void)snprintf(cfg_path, sizeof(cfg_path), "%s", cfg.config_file);
    preparse_config_path(argc, argv, cfg_path, sizeof(cfg_path));
    (void)runtime_config_load_file(&cfg, cfg_path);

    runtime_config_apply_env(&cfg);

    int should_exit = 0;
    if (runtime_config_apply_cli(&cfg, argc, argv, &should_exit) < 0) return EXIT_FAILURE;
    if (should_exit) return EXIT_SUCCESS;

    if (logger_init(cfg.log_path) < 0) {
        (void)fprintf(stderr, "logger_init(%s) failed: %s\n", cfg.log_path, strerror(errno));
        return EXIT_FAILURE;
    }

    runtime_config_log_environment();
    runtime_config_log_summary(&cfg);

    const int rc = server_run(&cfg);
    logger_close();
    return rc;
}
