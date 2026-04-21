#ifndef SERVER_H
#define SERVER_H

#include "common.h"
#include "job.h"
#include "protocol.h"
#include "queue.h"
#include "runtime_config.h"
#include "worker.h"

#define MAX_BLOCKED_IPS 32

typedef struct client_session {
    int fd;
    client_type_t type;
    bool active;
    bool kick_requested;
    char addr_str[64];
    struct timespec connected_at;
} client_session_t;

typedef struct blocklist {
    char ips[MAX_BLOCKED_IPS][64];
    size_t count;
    pthread_mutex_t mutex;
} blocklist_t;

typedef struct server_state {
    int listen_fd_user;
    int listen_fd_admin;
    int pipe_read_fd;
    int pipe_write_fd;
    volatile sig_atomic_t running;
    job_queue_t queue;
    job_table_t jobs;
    blocklist_t blocklist;
    pthread_t workers[MAX_WORKERS];
    worker_context_t worker_ctx[MAX_WORKERS];
    runtime_config_t config;
} server_state_t;

int server_run(const runtime_config_t *cfg);

#endif
