#ifndef RUNTIME_CONFIG_H
#define RUNTIME_CONFIG_H

#include "common.h"

#define CFG_PATH_MAX 256

typedef struct runtime_config {
    int user_port;
    int admin_port;
    int worker_count;
    char log_path[CFG_PATH_MAX];
    char storage_root[CFG_PATH_MAX];
    char config_file[CFG_PATH_MAX];
    uint64_t max_upload_bytes;
} runtime_config_t;

void runtime_config_set_defaults(runtime_config_t *cfg);
int  runtime_config_load_file(runtime_config_t *cfg, const char *path);
void runtime_config_apply_env(runtime_config_t *cfg);
int  runtime_config_apply_cli(runtime_config_t *cfg, int argc, char *argv[], int *should_exit);
void runtime_config_log_summary(const runtime_config_t *cfg);
void runtime_config_log_environment(void);
void runtime_config_print_help(const char *prog);
void runtime_config_print_version(void);

#endif
