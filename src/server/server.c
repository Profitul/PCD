#include "config.h"
#include "logger.h"
#include "net.h"
#include "protocol.h"
#include "server.h"
#include "storage.h"

static volatile sig_atomic_t g_running = 1; /* flag global pentru oprirea graceful a serverului */

/* Handler de semnal pentru SIGINT/SIGTERM: seteaza doar un flag async-signal-safe. */
static void handle_signal(const int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        g_running = 0;
    }
}

/* Instaleaza handler pentru SIGINT/SIGTERM si ignora SIGPIPE. */
static int install_signal_handlers(void) {
    struct sigaction sa;
    (void)memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;

    if (sigaction(SIGINT, &sa, NULL) < 0) {
        return -1;
    }
    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        return -1;
    }

    struct sigaction sp;
    (void)memset(&sp, 0, sizeof(sp));
    sp.sa_handler = SIG_IGN;

    if (sigaction(SIGPIPE, &sp, NULL) < 0) {
        return -1;
    }

    return 0;
}

/* Formateaza si trimite un raspuns text pe socket. */
static int send_responsef(const int fd, const char *fmt, ...) {
    if (fmt == NULL) {
        return -1;
    }

    char buffer[BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    const int rc = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (rc < 0) {
        return -1;
    }
    if ((size_t)rc >= sizeof(buffer)) {
        errno = EMSGSIZE;
        return -1;
    }

    return (write_all(fd, buffer, strlen(buffer)) < 0) ? -1 : 0;
}

/* Parseaza un intreg pozitiv dintr-un string. Folosit pentru comanda admin KICK. */
static int parse_int_arg(const char *text, int *out) {
    if (text == NULL || out == NULL || text[0] == '\0') {
        return -1;
    }

    char *end = NULL;
    errno = 0;
    const long parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < 0L || parsed > INT32_MAX) {
        return -1;
    }

    *out = (int)parsed;
    return 0;
}

/* Initializeaza lista de IP-uri blocate. */
static int blocklist_init(blocklist_t *bl) {
    if (bl == NULL) {
        return -1;
    }

    (void)memset(bl, 0, sizeof(*bl));
    return (pthread_mutex_init(&bl->mutex, NULL) == 0) ? 0 : -1;
}

static void blocklist_destroy(blocklist_t *bl) {
    if (bl == NULL) {
        return;
    }

    (void)pthread_mutex_destroy(&bl->mutex);
}

/* Adauga un IP in blocklist daca nu exista deja. */
static int blocklist_add(blocklist_t *bl, const char *ip) {
    if (bl == NULL || ip == NULL || ip[0] == '\0') {
        return -1;
    }

    int rc = -1;
    (void)pthread_mutex_lock(&bl->mutex);

    int found = 0;
    for (size_t i = 0U; i < bl->count; i++) {
        if (strcmp(bl->ips[i], ip) == 0) {
            found = 1;
            break;
        }
    }

    if (found) {
        rc = 0;
    } else if (bl->count < MAX_BLOCKED_IPS) {
        (void)snprintf(bl->ips[bl->count], sizeof(bl->ips[0]), "%s", ip);
        bl->count++;
        rc = 0;
    }

    (void)pthread_mutex_unlock(&bl->mutex);
    return rc;
}

/* Sterge un IP din blocklist prin compactarea array-ului. */
static int blocklist_remove(blocklist_t *bl, const char *ip) {
    if (bl == NULL || ip == NULL || ip[0] == '\0') {
        return -1;
    }

    int rc = -1;
    (void)pthread_mutex_lock(&bl->mutex);

    for (size_t i = 0U; i < bl->count; i++) {
        if (strcmp(bl->ips[i], ip) == 0) {
            for (size_t j = i + 1U; j < bl->count; j++) {
                (void)memmove(bl->ips[j - 1U], bl->ips[j], sizeof(bl->ips[0]));
            }
            bl->count--;
            rc = 0;
            break;
        }
    }

    (void)pthread_mutex_unlock(&bl->mutex);
    return rc;
}

static int blocklist_contains(blocklist_t *bl, const char *ip) {
    if (bl == NULL || ip == NULL) {
        return 0;
    }

    int found = 0;
    (void)pthread_mutex_lock(&bl->mutex);

    for (size_t i = 0U; i < bl->count; i++) {
        if (strcmp(bl->ips[i], ip) == 0) {
            found = 1;
            break;
        }
    }

    (void)pthread_mutex_unlock(&bl->mutex);
    return found;
}

static int blocklist_format(blocklist_t *bl, char *out, const size_t out_size) {
    if (bl == NULL || out == NULL || out_size == 0U) {
        return -1;
    }

    out[0] = '\0';
    size_t used = 0U;

    (void)pthread_mutex_lock(&bl->mutex);

    if (bl->count == 0U) {
        (void)snprintf(out, out_size, "empty");
        (void)pthread_mutex_unlock(&bl->mutex);
        return 0;
    }

    for (size_t i = 0U; i < bl->count; i++) {
        const size_t item_len = strlen(bl->ips[i]);
        const size_t sep = (used == 0U) ? 0U : 1U;
        if (used + sep + item_len + 1U >= out_size) {
            break;
        }
        if (used != 0U) {
            out[used] = ',';
            used++;
            out[used] = '\0';
        }
        (void)snprintf(out + used, out_size - used, "%s", bl->ips[i]);
        used += item_len;
    }

    (void)pthread_mutex_unlock(&bl->mutex);
    return 0;
}

/* Inchide conexiunea clientului si reseteaza sesiunea. */
static void close_client(client_session_t *client) {
    if (client == NULL || !client->active) {
        return;
    }

    logger_log(LOG_LEVEL_INFO, "Client disconnected fd=%d addr=%s", client->fd, client->addr_str);
    (void)close(client->fd);
    client->fd = -1;
    client->active = false;
    client->type = CLIENT_TYPE_UNKNOWN;
    client->kick_requested = false;
    (void)memset(client->addr_str, 0, sizeof(client->addr_str));
}

/* Gaseste un slot liber si initializeaza sesiunea noua. */
static int add_client(client_session_t clients[], const int client_fd,
                      const client_type_t type, const char *addr_str) {
    if (clients == NULL) {
        return -1;
    }

    for (size_t i = 0U; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].fd = client_fd;
            clients[i].type = type;
            clients[i].active = true;
            clients[i].kick_requested = false;
            (void)clock_gettime(CLOCK_REALTIME, &clients[i].connected_at);
            (void)snprintf(clients[i].addr_str, sizeof(clients[i].addr_str), "%s",
                           (addr_str != NULL) ? addr_str : "unknown");
            return 0;
        }
    }

    return -1;
}

/* Reconstruieste array-ul pollfd inainte de fiecare apel poll().
   Ordinea fixa: INET user, INET admin, UNIX user, pipe worker, INotify, clienti activi. */
static void rebuild_pollfds(const server_state_t *state, client_session_t clients[],
                            struct pollfd pfds[], nfds_t *count) {
    nfds_t idx = 0U;

    pfds[idx].fd = state->listen_fd_user;
    pfds[idx].events = POLLIN;
    pfds[idx].revents = 0;
    idx++;

    pfds[idx].fd = state->listen_fd_admin;
    pfds[idx].events = POLLIN;
    pfds[idx].revents = 0;
    idx++;

    pfds[idx].fd = state->listen_fd_unix;
    pfds[idx].events = POLLIN;
    pfds[idx].revents = 0;
    idx++;

    pfds[idx].fd = state->pipe_read_fd;
    pfds[idx].events = POLLIN;
    pfds[idx].revents = 0;
    idx++;

    pfds[idx].fd = state->inotify_fd;
    pfds[idx].events = POLLIN;
    pfds[idx].revents = 0;
    idx++;

    for (size_t i = 0U; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            pfds[idx].fd = clients[i].fd;
            pfds[idx].events = POLLIN;
            pfds[idx].revents = 0;
            idx++;
        }
    }

    *count = idx;
}

/* Dreneaza evenimentele trimise de workeri pe pipe si le logheaza. */
static void handle_worker_pipe(server_state_t *state) {
    worker_event_t event;

    while (1) {
        const ssize_t rc = read(state->pipe_read_fd, &event, sizeof(event));
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            logger_log(LOG_LEVEL_ERROR, "Failed to read from worker pipe errno=%d", errno);
            break;
        }

        if (rc == 0) {
            break;
        }

        if ((size_t)rc != sizeof(event)) {
            logger_log(LOG_LEVEL_WARN, "Partial event read from worker pipe");
            continue;
        }

        logger_log(LOG_LEVEL_INFO, "Event job_id=%llu state=%s",
                   (unsigned long long)event.job_id,
                   job_state_to_string(event.state));
    }
}

/* Returneaza directorul monitorizat in functie de watch descriptor. */
static const char *inotify_watch_name(const server_state_t *state, const int wd) {
    if (wd == state->inotify_wd_uploads) {
        return storage_uploads_dir();
    }
    if (wd == state->inotify_wd_results) {
        return storage_results_dir();
    }
    if (wd == state->inotify_wd_temp) {
        return storage_temp_dir();
    }
    return "unknown";
}

/* Adauga o eticheta de eveniment in buffer-ul text, fara strcat/strcpy. */
static void append_event_label(char *out, const size_t out_size, size_t *used, const char *label) {
    if (out == NULL || used == NULL || label == NULL || out_size == 0U) {
        return;
    }

    const size_t label_len = strlen(label);
    const size_t sep = (*used == 0U) ? 0U : 1U;
    if (*used + sep + label_len + 1U >= out_size) {
        return;
    }

    if (*used != 0U) {
        out[*used] = '|';
        (*used)++;
        out[*used] = '\0';
    }

    (void)snprintf(out + *used, out_size - *used, "%s", label);
    *used += label_len;
}

/* Converteste masca INotify intr-un string lizibil pentru log. */
static void inotify_mask_to_string(const uint32_t mask, char *out, const size_t out_size) {
    if (out == NULL || out_size == 0U) {
        return;
    }

    out[0] = '\0';
    size_t used = 0U;

    if ((mask & IN_CREATE) != 0U)      { append_event_label(out, out_size, &used, "CREATE"); }
    if ((mask & IN_CLOSE_WRITE) != 0U) { append_event_label(out, out_size, &used, "CLOSE_WRITE"); }
    if ((mask & IN_MODIFY) != 0U)      { append_event_label(out, out_size, &used, "MODIFY"); }
    if ((mask & IN_DELETE) != 0U)      { append_event_label(out, out_size, &used, "DELETE"); }
    if ((mask & IN_MOVED_FROM) != 0U)  { append_event_label(out, out_size, &used, "MOVED_FROM"); }
    if ((mask & IN_MOVED_TO) != 0U)    { append_event_label(out, out_size, &used, "MOVED_TO"); }
    if ((mask & IN_ATTRIB) != 0U)      { append_event_label(out, out_size, &used, "ATTRIB"); }
    if ((mask & IN_Q_OVERFLOW) != 0U)  { append_event_label(out, out_size, &used, "Q_OVERFLOW"); }
    if ((mask & IN_IGNORED) != 0U)     { append_event_label(out, out_size, &used, "IGNORED"); }

    if (used == 0U) {
        (void)snprintf(out, out_size, "mask=0x%08x", mask);
    }
}

/* Initializeaza INotify si urmareste directoarele de lucru ale serverului. */
static int init_inotify(server_state_t *state) {
    state->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (state->inotify_fd < 0) {
        return -1;
    }

    state->inotify_wd_uploads = inotify_add_watch(state->inotify_fd, storage_uploads_dir(), INOTIFY_WATCH_MASK);
    if (state->inotify_wd_uploads < 0) {
        return -1;
    }

    state->inotify_wd_results = inotify_add_watch(state->inotify_fd, storage_results_dir(), INOTIFY_WATCH_MASK);
    if (state->inotify_wd_results < 0) {
        return -1;
    }

    state->inotify_wd_temp = inotify_add_watch(state->inotify_fd, storage_temp_dir(), INOTIFY_WATCH_MASK);
    if (state->inotify_wd_temp < 0) {
        return -1;
    }

    logger_log(LOG_LEVEL_INFO, "INotify active uploads=%s results=%s temp=%s",
               storage_uploads_dir(), storage_results_dir(), storage_temp_dir());
    return 0;
}

/* Dreneaza si logheaza evenimentele INotify. */
static void handle_inotify_events(server_state_t *state) {
    union inotify_event_buffer {
        char bytes[INOTIFY_EVENT_BUFFER_SIZE];
        struct inotify_event align;
    } buffer;

    while (1) {
        const ssize_t len = read(state->inotify_fd, buffer.bytes, sizeof(buffer.bytes));
        if (len < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            logger_log(LOG_LEVEL_ERROR, "INotify read failed errno=%d", errno);
            break;
        }

        if (len == 0) {
            break;
        }

        size_t pos = 0U;
        while (pos < (size_t)len) {
            const struct inotify_event *event =
                (const struct inotify_event *)(const void *)(buffer.bytes + pos);

            char mask_text[SMALL_BUFFER_SIZE];
            inotify_mask_to_string(event->mask, mask_text, sizeof(mask_text));

            const char *name = (event->len > 0U && event->name[0] != '\0') ? event->name : "(none)";
            logger_log(LOG_LEVEL_INFO, "INotify dir=%s event=%s name=%s cookie=%u",
                       inotify_watch_name(state, event->wd), mask_text, name, event->cookie);

            pos += sizeof(struct inotify_event) + event->len;
        }
    }
}

/* Opreste INotify si inchide descriptorul. */
static void close_inotify(server_state_t *state) {
    if (state == NULL || state->inotify_fd < 0) {
        return;
    }

    if (state->inotify_wd_uploads >= 0) {
        (void)inotify_rm_watch(state->inotify_fd, state->inotify_wd_uploads);
        state->inotify_wd_uploads = -1;
    }
    if (state->inotify_wd_results >= 0) {
        (void)inotify_rm_watch(state->inotify_fd, state->inotify_wd_results);
        state->inotify_wd_results = -1;
    }
    if (state->inotify_wd_temp >= 0) {
        (void)inotify_rm_watch(state->inotify_fd, state->inotify_wd_temp);
        state->inotify_wd_temp = -1;
    }

    (void)close(state->inotify_fd);
    state->inotify_fd = -1;
}

/* Creeaza un job nou si il adauga in coada de procesare. */
static int enqueue_job(server_state_t *state, client_session_t *client,
                       const job_type_t type, const char *payload,
                       const char *input_path, const char *stored_path) {
    job_t *job = job_table_create_job(&state->jobs, client->fd, client->addr_str,
                                      type, payload, input_path);
    if (job == NULL) {
        return send_responsef(client->fd, "ERR cannot create job\n");
    }

    if (stored_path != NULL) {
        job_set_stored_path(job, stored_path);
    }

    if (job_queue_push(&state->queue, job) < 0) {
        job_set_result(job, "enqueue failed");
        job_set_state(job, JOB_STATE_FAILED);
        return send_responsef(client->fd, "ERR cannot enqueue job\n");
    }

    logger_log(LOG_LEVEL_INFO, "Submitted job_id=%llu type=%s from fd=%d addr=%s",
               (unsigned long long)job->id, job_type_to_string(type),
               client->fd, client->addr_str);

    return send_responsef(client->fd, "JOB %llu\n", (unsigned long long)job->id);
}

/* Primeste un PNG de la client si il salveaza in storage/uploads. */
static int receive_png_to_upload(server_state_t *state, client_session_t *client,
                                 const uint64_t png_size, char *out_path,
                                 const size_t out_path_size) {
    if (png_size == 0U || png_size > state->config.max_upload_bytes) {
        (void)send_responsef(client->fd, "ERR invalid png size\n");
        return -1;
    }

    uint64_t next_id;
    (void)pthread_mutex_lock(&state->jobs.mutex);
    next_id = state->jobs.next_id;
    (void)pthread_mutex_unlock(&state->jobs.mutex);

    if (storage_make_upload_path(next_id, "png", out_path, out_path_size) < 0) {
        (void)send_responsef(client->fd, "ERR path build failed\n");
        return -1;
    }

    logger_log(LOG_LEVEL_INFO, "Receiving PNG fd=%d addr=%s bytes=%llu path=%s",
               client->fd, client->addr_str, (unsigned long long)png_size, out_path);

    if (storage_receive_to_file(client->fd, (size_t)png_size, out_path) < 0) {
        (void)send_responsef(client->fd, "ERR upload failed\n");
        return -1;
    }

    return 0;
}

/* Primeste un fisier auxiliar de la client. */
static int receive_extra_to_file(const int fd, const uint64_t size,
                                 const uint64_t max, const char *label,
                                 char *out_path, const size_t out_path_size,
                                 const uint64_t job_seq, const char *suffix) {
    if (size > max) {
        (void)send_responsef(fd, "ERR %s too large\n", label);
        return -1;
    }

    if (storage_make_upload_path(job_seq, suffix, out_path, out_path_size) < 0) {
        (void)send_responsef(fd, "ERR path build failed\n");
        return -1;
    }

    if (size == 0U) {
        FILE *fp = fopen(out_path, "wb");
        if (fp == NULL) {
            (void)send_responsef(fd, "ERR cannot create file\n");
            return -1;
        }
        if (fclose(fp) != 0) {
            (void)send_responsef(fd, "ERR cannot close file\n");
            return -1;
        }
        return 0;
    }

    if (storage_receive_to_file(fd, (size_t)size, out_path) < 0) {
        (void)send_responsef(fd, "ERR %s receive failed\n", label);
        return -1;
    }

    return 0;
}

static int send_help(const int fd) {
    static const char *msg =
        "OK help user: PING QUIT | ENCODE_TEXT <png_len> <text_len> | "
        "ENCODE_FILE <png_len> <name_len> <file_len> | DECODE <png_len> | "
        "VALIDATE <png_len> | CAPACITY <png_len> | STATUS <id> | RESULT <id> | "
        "META <id> | DOWNLOAD <id> | CANCEL <id>\n";
    return send_responsef(fd, "%s", msg);
}

static int send_admin_help(const int fd) {
    static const char *msg =
        "OK help admin: PING QUIT STATS LISTJOBS HISTORY AVGDURATION LISTCLIENTS | "
        "CANCEL <id> | KICK <fd> | BLOCKIP <ip> | UNBLOCKIP <ip>\n";
    return send_responsef(fd, "%s", msg);
}

static int handle_user_command(server_state_t *state, client_session_t *client,
                               const protocol_request_t *request);
static int handle_admin_command(server_state_t *state, client_session_t *clients,
                                client_session_t *admin, const protocol_request_t *request);

static int handle_user_command(server_state_t *state, client_session_t *client,
                               const protocol_request_t *request) {
    switch (request->command) {
        case PROTO_CMD_PING:
            return send_responsef(client->fd, "PONG\n");

        case PROTO_CMD_HELP:
            return send_help(client->fd);

        case PROTO_CMD_SUBMIT:
            if (request->argument1[0] == '\0') {
                return send_responsef(client->fd, "ERR empty payload\n");
            }
            return enqueue_job(state, client, JOB_TYPE_TEXT, request->argument1, NULL, NULL);

        case PROTO_CMD_ENCODE_TEXT: {
            char png_path[MAX_JOB_PATH];
            if (receive_png_to_upload(state, client, request->size1, png_path, sizeof(png_path)) < 0) {
                return 0;
            }

            uint64_t next_id;
            (void)pthread_mutex_lock(&state->jobs.mutex);
            next_id = state->jobs.next_id;
            (void)pthread_mutex_unlock(&state->jobs.mutex);

            char text_path[MAX_JOB_PATH];
            if (receive_extra_to_file(client->fd, request->size2, MAX_TEXT_BYTES,
                                      "text", text_path, sizeof(text_path),
                                      next_id, "txt") < 0) {
                return 0;
            }
            return enqueue_job(state, client, JOB_TYPE_ENCODE_TEXT, NULL, png_path, text_path);
        }

        case PROTO_CMD_ENCODE_FILE: {
            if (request->size2 > MAX_FILENAME_BYTES) {
                (void)discard_exact(client->fd, (size_t)(request->size1 + request->size2 + request->size3));
                return send_responsef(client->fd, "ERR filename too long\n");
            }

            char png_path[MAX_JOB_PATH];
            if (receive_png_to_upload(state, client, request->size1, png_path, sizeof(png_path)) < 0) {
                return 0;
            }

            char filename[MAX_JOB_FILENAME];
            (void)memset(filename, 0, sizeof(filename));
            if (request->size2 > 0U) {
                if (read_exact(client->fd, filename, (size_t)request->size2) != (ssize_t)request->size2) {
                    return send_responsef(client->fd, "ERR filename read failed\n");
                }
            }

            uint64_t next_id;
            (void)pthread_mutex_lock(&state->jobs.mutex);
            next_id = state->jobs.next_id;
            (void)pthread_mutex_unlock(&state->jobs.mutex);

            char file_path[MAX_JOB_PATH];
            if (receive_extra_to_file(client->fd, request->size3, state->config.max_upload_bytes,
                                      "file", file_path, sizeof(file_path),
                                      next_id, "data") < 0) {
                return 0;
            }
            return enqueue_job(state, client, JOB_TYPE_ENCODE_FILE, filename, png_path, file_path);
        }

        case PROTO_CMD_DECODE: {
            char png_path[MAX_JOB_PATH];
            if (receive_png_to_upload(state, client, request->size1, png_path, sizeof(png_path)) < 0) {
                return 0;
            }
            return enqueue_job(state, client, JOB_TYPE_DECODE, NULL, png_path, NULL);
        }

        case PROTO_CMD_VALIDATE: {
            char png_path[MAX_JOB_PATH];
            if (receive_png_to_upload(state, client, request->size1, png_path, sizeof(png_path)) < 0) {
                return 0;
            }
            return enqueue_job(state, client, JOB_TYPE_VALIDATE_IMAGE, NULL, png_path, NULL);
        }

        case PROTO_CMD_CAPACITY: {
            char png_path[MAX_JOB_PATH];
            if (receive_png_to_upload(state, client, request->size1, png_path, sizeof(png_path)) < 0) {
                return 0;
            }
            return enqueue_job(state, client, JOB_TYPE_ANALYZE_CAPACITY, NULL, png_path, NULL);
        }

        case PROTO_CMD_VALIDATE_IMAGE:
            if (request->argument1[0] == '\0') {
                return send_responsef(client->fd, "ERR empty path\n");
            }
            return enqueue_job(state, client, JOB_TYPE_VALIDATE_IMAGE, NULL, request->argument1, NULL);

        case PROTO_CMD_ANALYZE_CAPACITY:
            if (request->argument1[0] == '\0') {
                return send_responsef(client->fd, "ERR empty path\n");
            }
            return enqueue_job(state, client, JOB_TYPE_ANALYZE_CAPACITY, NULL, request->argument1, NULL);

        case PROTO_CMD_STATUS: {
            job_t *job = job_table_find(&state->jobs, request->job_id);
            if (job == NULL) {
                return send_responsef(client->fd, "ERR job not found\n");
            }
            return send_responsef(client->fd, "STATUS %llu %s\n",
                                  (unsigned long long)request->job_id,
                                  job_state_to_string(job_get_state(job)));
        }

        case PROTO_CMD_RESULT: {
            job_t *job = job_table_find(&state->jobs, request->job_id);
            if (job == NULL) {
                return send_responsef(client->fd, "ERR job not found\n");
            }
            const job_state_t st = job_get_state(job);
            if (st != JOB_STATE_DONE && st != JOB_STATE_FAILED && st != JOB_STATE_CANCELED) {
                return send_responsef(client->fd, "ERR job not finished state=%s\n",
                                      job_state_to_string(st));
            }
            char result[MAX_JOB_RESULT];
            job_get_result(job, result, sizeof(result));
            return send_responsef(client->fd, "RESULT %llu state=%s msg=%s\n",
                                  (unsigned long long)request->job_id,
                                  job_state_to_string(st), result);
        }

        case PROTO_CMD_META: {
            job_t *job = job_table_find(&state->jobs, request->job_id);
            if (job == NULL) {
                return send_responsef(client->fd, "ERR job not found\n");
            }
            if (job_get_state(job) != JOB_STATE_DONE) {
                return send_responsef(client->fd, "ERR job not done\n");
            }
            job_result_kind_t kind;
            size_t size;
            char filename[MAX_JOB_FILENAME];
            job_get_result_meta(job, &kind, &size, filename, sizeof(filename));
            return send_responsef(client->fd, "META %llu type=%s size=%zu name=%s\n",
                                  (unsigned long long)request->job_id,
                                  job_result_kind_to_string(kind), size, filename);
        }

        case PROTO_CMD_DOWNLOAD: {
            job_t *job = job_table_find(&state->jobs, request->job_id);
            if (job == NULL) {
                return send_responsef(client->fd, "ERR job not found\n");
            }
            if (job_get_state(job) != JOB_STATE_DONE) {
                return send_responsef(client->fd, "ERR job not done\n");
            }

            char path[MAX_JOB_PATH];
            job_get_output_path(job, path, sizeof(path));
            if (path[0] == '\0') {
                return send_responsef(client->fd, "ERR no output\n");
            }

            off_t size = 0;
            if (get_file_size_bytes(path, &size) < 0) {
                return send_responsef(client->fd, "ERR stat failed\n");
            }

            if (send_responsef(client->fd, "DATA %lld\n", (long long)size) < 0) {
                return -1;
            }
            if (storage_send_file(client->fd, path) < 0) {
                logger_log(LOG_LEVEL_WARN, "send_file failed job=%llu",
                           (unsigned long long)request->job_id);
                return -1;
            }
            return 0;
        }

        case PROTO_CMD_CANCEL: {
            job_t *job = job_table_find(&state->jobs, request->job_id);
            if (job == NULL) {
                return send_responsef(client->fd, "ERR job not found\n");
            }
            if (!job_request_cancel(job)) {
                return send_responsef(client->fd, "ERR cannot cancel job\n");
            }
            logger_log(LOG_LEVEL_WARN, "Cancel by user fd=%d job=%llu",
                       client->fd, (unsigned long long)request->job_id);
            return send_responsef(client->fd, "OK cancel requested %llu\n",
                                  (unsigned long long)request->job_id);
        }

        case PROTO_CMD_QUIT:
            (void)send_responsef(client->fd, "BYE\n");
            close_client(client);
            return 0;

        case PROTO_CMD_LISTJOBS:
        case PROTO_CMD_STATS:
        case PROTO_CMD_LISTCLIENTS:
        case PROTO_CMD_HISTORY:
        case PROTO_CMD_AVGDURATION:
        case PROTO_CMD_KICK:
        case PROTO_CMD_BLOCKIP:
        case PROTO_CMD_UNBLOCKIP:
            return send_responsef(client->fd, "ERR admin command\n");

        default:
            return send_responsef(client->fd, "ERR unknown command\n");
    }
}

static int format_client_list(client_session_t clients[], char *buf, const size_t buf_size) {
    if (clients == NULL || buf == NULL || buf_size == 0U) {
        return -1;
    }

    buf[0] = '\0';
    size_t used = 0U;
    size_t active = 0U;

    for (size_t i = 0U; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            continue;
        }

        active++;
        char item[SMALL_BUFFER_SIZE];
        (void)snprintf(item, sizeof(item), "fd=%d,type=%s,addr=%s",
                       clients[i].fd,
                       (clients[i].type == CLIENT_TYPE_ADMIN) ? "admin" : "user",
                       clients[i].addr_str);

        const size_t item_len = strlen(item);
        const size_t sep = (used == 0U) ? 0U : 1U;
        if (used + sep + item_len + 1U >= buf_size) {
            break;
        }

        if (used != 0U) {
            buf[used] = ';';
            used++;
            buf[used] = '\0';
        }
        (void)snprintf(buf + used, buf_size - used, "%s", item);
        used += item_len;
    }

    if (active == 0U) {
        (void)snprintf(buf, buf_size, "empty");
    }

    return 0;
}

static int handle_admin_command(server_state_t *state, client_session_t *clients,
                                client_session_t *admin, const protocol_request_t *request) {
    switch (request->command) {
        case PROTO_CMD_PING:
            return send_responsef(admin->fd, "PONG\n");

        case PROTO_CMD_HELP:
            return send_admin_help(admin->fd);

        case PROTO_CMD_STATS: {
            job_stats_t st;
            job_table_collect_stats(&state->jobs, &st);

            size_t active_clients = 0U;
            for (size_t i = 0U; i < MAX_CLIENTS; i++) {
                if (clients[i].active) {
                    active_clients++;
                }
            }

            return send_responsef(admin->fd,
                "STATS total=%zu queued=%zu running=%zu done=%zu failed=%zu canceled=%zu avg_ms=%.1f clients=%zu\n",
                st.total, st.queued, st.running, st.done, st.failed, st.canceled,
                st.avg_duration_ms, active_clients);
        }

        case PROTO_CMD_AVGDURATION: {
            job_stats_t st;
            job_table_collect_stats(&state->jobs, &st);
            return send_responsef(admin->fd, "AVGDURATION ms=%.2f samples=%zu\n",
                                  st.avg_duration_ms, st.done + st.failed);
        }

        case PROTO_CMD_LISTJOBS: {
            char list[BUFFER_SIZE * 2U];
            if (job_table_format_list(&state->jobs, list, sizeof(list)) < 0) {
                return send_responsef(admin->fd, "ERR list failed\n");
            }
            return send_responsef(admin->fd, "JOBS %s\n", list);
        }

        case PROTO_CMD_HISTORY: {
            char hist[BUFFER_SIZE * 2U];
            if (job_table_format_history(&state->jobs, hist, sizeof(hist), 50U) < 0) {
                return send_responsef(admin->fd, "ERR history failed\n");
            }
            return send_responsef(admin->fd, "HISTORY %s\n", hist);
        }

        case PROTO_CMD_LISTCLIENTS: {
            char list[BUFFER_SIZE * 2U];
            (void)format_client_list(clients, list, sizeof(list));
            return send_responsef(admin->fd, "CLIENTS %s\n", list);
        }

        case PROTO_CMD_CANCEL: {
            job_t *job = job_table_find(&state->jobs, request->job_id);
            if (job == NULL) {
                return send_responsef(admin->fd, "ERR job not found\n");
            }
            if (!job_request_cancel(job)) {
                return send_responsef(admin->fd, "ERR cannot cancel\n");
            }
            logger_log(LOG_LEVEL_WARN, "Cancel by admin job=%llu",
                       (unsigned long long)request->job_id);
            return send_responsef(admin->fd, "OK cancel requested %llu\n",
                                  (unsigned long long)request->job_id);
        }

        case PROTO_CMD_KICK: {
            int target_fd = -1;
            if (parse_int_arg(request->argument1, &target_fd) < 0) {
                return send_responsef(admin->fd, "ERR invalid fd\n");
            }

            int found = 0;
            for (size_t i = 0U; i < MAX_CLIENTS; i++) {
                if (clients[i].active && clients[i].fd == target_fd &&
                    clients[i].type == CLIENT_TYPE_USER) {
                    clients[i].kick_requested = true;
                    (void)send_responsef(target_fd, "ERR kicked by admin\n");
                    found = 1;
                    logger_log(LOG_LEVEL_WARN, "Admin kicked fd=%d addr=%s",
                               target_fd, clients[i].addr_str);
                    break;
                }
            }

            if (!found) {
                return send_responsef(admin->fd, "ERR client not found\n");
            }
            return send_responsef(admin->fd, "OK kicked fd=%d\n", target_fd);
        }

        case PROTO_CMD_BLOCKIP: {
            if (request->argument1[0] == '\0') {
                return send_responsef(admin->fd, "ERR empty ip\n");
            }
            if (blocklist_add(&state->blocklist, request->argument1) < 0) {
                return send_responsef(admin->fd, "ERR blocklist full\n");
            }

            int kicked = 0;
            for (size_t i = 0U; i < MAX_CLIENTS; i++) {
                if (clients[i].active && clients[i].type == CLIENT_TYPE_USER &&
                    strcmp(clients[i].addr_str, request->argument1) == 0) {
                    clients[i].kick_requested = true;
                    kicked++;
                }
            }

            logger_log(LOG_LEVEL_WARN, "Admin blocked ip=%s kicked=%d", request->argument1, kicked);
            return send_responsef(admin->fd, "OK blocked ip=%s kicked=%d\n",
                                  request->argument1, kicked);
        }

        case PROTO_CMD_UNBLOCKIP: {
            if (request->argument1[0] == '\0') {
                return send_responsef(admin->fd, "ERR empty ip\n");
            }
            if (blocklist_remove(&state->blocklist, request->argument1) < 0) {
                return send_responsef(admin->fd, "ERR not in blocklist\n");
            }

            char bl[BUFFER_SIZE];
            (void)blocklist_format(&state->blocklist, bl, sizeof(bl));
            return send_responsef(admin->fd, "OK unblocked ip=%s blocklist=%s\n",
                                  request->argument1, bl);
        }

        case PROTO_CMD_QUIT:
            (void)send_responsef(admin->fd, "BYE\n");
            close_client(admin);
            return 0;

        default:
            return send_responsef(admin->fd, "ERR unknown admin command\n");
    }
}

static void handle_client_command(server_state_t *state,
                                  client_session_t *clients,
                                  client_session_t *client) {
    char buffer[BUFFER_SIZE];
    const ssize_t rc = read_line(client->fd, buffer, sizeof(buffer));
    if (rc <= 0) {
        close_client(client);
        return;
    }

    protocol_request_t request;
    if (protocol_parse_line(buffer, &request) < 0) {
        (void)send_responsef(client->fd, "ERR parse failed\n");
        return;
    }

    if (client->type == CLIENT_TYPE_ADMIN) {
        (void)handle_admin_command(state, clients, client, &request);
    } else {
        (void)handle_user_command(state, client, &request);
    }
}

/* Accepta o conexiune INET, verifica blocklist si limita de admin unic. */
static int accept_new_inet_connection(server_state_t *state, const int listen_fd,
                                      client_session_t clients[],
                                      const client_type_t type,
                                      const bool single_admin_only) {
    struct sockaddr_in peer_addr;
    (void)memset(&peer_addr, 0, sizeof(peer_addr));
    socklen_t peer_len = (socklen_t)sizeof(peer_addr);

    const int client_fd = accept(listen_fd, (struct sockaddr *)&peer_addr, &peer_len);
    if (client_fd < 0) {
        return -1;
    }

    char addr_str[64];
    const char *res = inet_ntop(AF_INET, &peer_addr.sin_addr, addr_str, sizeof(addr_str));
    if (res == NULL) {
        (void)snprintf(addr_str, sizeof(addr_str), "unknown");
    }

    if (type == CLIENT_TYPE_USER && blocklist_contains(&state->blocklist, addr_str)) {
        (void)send_responsef(client_fd, "ERR ip blocked\n");
        (void)close(client_fd);
        logger_log(LOG_LEVEL_WARN, "Rejected connection from blocked ip=%s", addr_str);
        return 0;
    }

    if (single_admin_only) {
        for (size_t i = 0U; i < MAX_CLIENTS; i++) {
            if (clients[i].active && clients[i].type == CLIENT_TYPE_ADMIN) {
                (void)send_responsef(client_fd, "ERR admin already connected\n");
                (void)close(client_fd);
                logger_log(LOG_LEVEL_WARN, "Rejected second admin connection ip=%s", addr_str);
                return 0;
            }
        }
    }

    if (add_client(clients, client_fd, type, addr_str) < 0) {
        (void)send_responsef(client_fd, "ERR server full\n");
        (void)close(client_fd);
        logger_log(LOG_LEVEL_WARN, "Server full, rejected ip=%s", addr_str);
        return 0;
    }

    logger_log(LOG_LEVEL_INFO, "Accepted fd=%d type=%s addr=%s", client_fd,
               (type == CLIENT_TYPE_ADMIN) ? "admin" : "user", addr_str);
    (void)send_responsef(client_fd, "OK connected\n");
    return 0;
}

/* Accepta o conexiune UNIX/LOCAL/FILE pentru clienti user locali. */
static int accept_new_unix_connection(server_state_t *state, client_session_t clients[]) {
    const int client_fd = accept(state->listen_fd_unix, NULL, NULL);
    if (client_fd < 0) {
        return -1;
    }

    const char *addr_str = "unix-local";
    if (add_client(clients, client_fd, CLIENT_TYPE_USER, addr_str) < 0) {
        (void)send_responsef(client_fd, "ERR server full\n");
        (void)close(client_fd);
        logger_log(LOG_LEVEL_WARN, "Server full, rejected unix client");
        return 0;
    }

    logger_log(LOG_LEVEL_INFO, "Accepted fd=%d type=user addr=%s socket=%s",
               client_fd, addr_str, state->config.unix_socket_path);
    (void)send_responsef(client_fd, "OK connected\n");
    return 0;
}

/* Porneste worker_count thread-uri. */
static int init_workers(server_state_t *state) {
    for (int i = 0; i < state->config.worker_count; i++) {
        state->worker_ctx[i].queue = &state->queue;
        state->worker_ctx[i].table = &state->jobs;
        state->worker_ctx[i].notify_fd = state->pipe_write_fd;
        state->worker_ctx[i].running = &state->running;
        state->worker_ctx[i].worker_index = (size_t)i;

        if (pthread_create(&state->workers[i], NULL, worker_main, &state->worker_ctx[i]) != 0) {
            return -1;
        }
    }

    return 0;
}

/* Semnalizeaza oprirea workerilor si asteapta terminarea thread-urilor. */
static void stop_workers(server_state_t *state) {
    state->running = 0;
    job_queue_stop(&state->queue);

    for (int i = 0; i < state->config.worker_count; i++) {
        (void)pthread_join(state->workers[i], NULL);
    }
}

/* Inchide toate resursele initializate in server_state_t. */
static void cleanup_server_state(server_state_t *state, client_session_t clients[],
                                 const int workers_started,
                                 const int queue_initialized,
                                 const int jobs_initialized,
                                 const int blocklist_initialized) {
    if (workers_started) {
        stop_workers(state);
    }

    if (clients != NULL) {
        for (size_t i = 0U; i < MAX_CLIENTS; i++) {
            close_client(&clients[i]);
        }
    }

    if (state->listen_fd_user >= 0) {
        (void)close(state->listen_fd_user);
        state->listen_fd_user = -1;
    }
    if (state->listen_fd_admin >= 0) {
        (void)close(state->listen_fd_admin);
        state->listen_fd_admin = -1;
    }
    if (state->listen_fd_unix >= 0) {
        (void)close(state->listen_fd_unix);
        state->listen_fd_unix = -1;
        (void)unlink(state->config.unix_socket_path);
    }
    if (state->pipe_read_fd >= 0) {
        (void)close(state->pipe_read_fd);
        state->pipe_read_fd = -1;
    }
    if (state->pipe_write_fd >= 0) {
        (void)close(state->pipe_write_fd);
        state->pipe_write_fd = -1;
    }

    close_inotify(state);

    if (queue_initialized) {
        job_queue_destroy(&state->queue);
    }
    if (jobs_initialized) {
        job_table_destroy(&state->jobs);
    }
    if (blocklist_initialized) {
        blocklist_destroy(&state->blocklist);
    }
}

/* Punctul central al serverului: initializeaza componentele si ruleaza bucla poll(). */
int server_run(const runtime_config_t *cfg) {
    if (cfg == NULL) {
        errno = EINVAL;
        return EXIT_FAILURE;
    }

    g_running = 1;

    server_state_t state;
    (void)memset(&state, 0, sizeof(state));
    state.listen_fd_user = -1;
    state.listen_fd_admin = -1;
    state.listen_fd_unix = -1;
    state.pipe_read_fd = -1;
    state.pipe_write_fd = -1;
    state.inotify_fd = -1;
    state.inotify_wd_uploads = -1;
    state.inotify_wd_results = -1;
    state.inotify_wd_temp = -1;
    state.running = 1;
    state.config = *cfg;

    client_session_t clients[MAX_CLIENTS];
    (void)memset(clients, 0, sizeof(clients));
    for (size_t i = 0U; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
    }

    int blocklist_initialized = 0;
    int jobs_initialized = 0;
    int queue_initialized = 0;
    int workers_started = 0;

    if (blocklist_init(&state.blocklist) < 0) {
        perror("blocklist_init");
        return EXIT_FAILURE;
    }
    blocklist_initialized = 1;

    if (install_signal_handlers() < 0) {
        perror("sigaction");
        cleanup_server_state(&state, clients, workers_started, queue_initialized,
                             jobs_initialized, blocklist_initialized);
        return EXIT_FAILURE;
    }

    if (storage_set_root(state.config.storage_root) < 0) {
        perror("storage_set_root");
        cleanup_server_state(&state, clients, workers_started, queue_initialized,
                             jobs_initialized, blocklist_initialized);
        return EXIT_FAILURE;
    }

    if (storage_init_dirs() < 0) {
        perror("storage_init");
        cleanup_server_state(&state, clients, workers_started, queue_initialized,
                             jobs_initialized, blocklist_initialized);
        return EXIT_FAILURE;
    }

    if (job_table_init(&state.jobs) < 0) {
        perror("job_table_init");
        cleanup_server_state(&state, clients, workers_started, queue_initialized,
                             jobs_initialized, blocklist_initialized);
        return EXIT_FAILURE;
    }
    jobs_initialized = 1;

    if (job_queue_init(&state.queue) < 0) {
        perror("job_queue_init");
        cleanup_server_state(&state, clients, workers_started, queue_initialized,
                             jobs_initialized, blocklist_initialized);
        return EXIT_FAILURE;
    }
    queue_initialized = 1;

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        cleanup_server_state(&state, clients, workers_started, queue_initialized,
                             jobs_initialized, blocklist_initialized);
        return EXIT_FAILURE;
    }
    state.pipe_read_fd = pipefd[0];
    state.pipe_write_fd = pipefd[1];

    if (set_nonblocking(state.pipe_read_fd) < 0) {
        perror("set_nonblocking pipe");
        cleanup_server_state(&state, clients, workers_started, queue_initialized,
                             jobs_initialized, blocklist_initialized);
        return EXIT_FAILURE;
    }

    if (init_inotify(&state) < 0) {
        perror("inotify");
        cleanup_server_state(&state, clients, workers_started, queue_initialized,
                             jobs_initialized, blocklist_initialized);
        return EXIT_FAILURE;
    }

    state.listen_fd_user = create_listen_socket((uint16_t)state.config.user_port);
    if (state.listen_fd_user < 0) {
        perror("listen user");
        cleanup_server_state(&state, clients, workers_started, queue_initialized,
                             jobs_initialized, blocklist_initialized);
        return EXIT_FAILURE;
    }

    state.listen_fd_admin = create_listen_socket((uint16_t)state.config.admin_port);
    if (state.listen_fd_admin < 0) {
        perror("listen admin");
        cleanup_server_state(&state, clients, workers_started, queue_initialized,
                             jobs_initialized, blocklist_initialized);
        return EXIT_FAILURE;
    }

    state.listen_fd_unix = create_unix_listen_socket(state.config.unix_socket_path);
    if (state.listen_fd_unix < 0) {
        perror("listen unix");
        cleanup_server_state(&state, clients, workers_started, queue_initialized,
                             jobs_initialized, blocklist_initialized);
        return EXIT_FAILURE;
    }

    if (init_workers(&state) < 0) {
        perror("pthread_create");
        cleanup_server_state(&state, clients, workers_started, queue_initialized,
                             jobs_initialized, blocklist_initialized);
        return EXIT_FAILURE;
    }
    workers_started = 1;

    logger_log(LOG_LEVEL_INFO,
               "Server started user_port=%d admin_port=%d unix_socket=%s workers=%d",
               state.config.user_port, state.config.admin_port,
               state.config.unix_socket_path, state.config.worker_count);

    while (g_running != 0) {
        struct pollfd pfds[MAX_POLL_FDS];
        nfds_t nfds = 0U;
        rebuild_pollfds(&state, clients, pfds, &nfds);

        const int ready = poll(pfds, nfds, 1000);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }

        if (ready > 0) {
            nfds_t idx = 0U;

            if ((pfds[idx].revents & POLLIN) != 0) {
                (void)accept_new_inet_connection(&state, state.listen_fd_user,
                                                 clients, CLIENT_TYPE_USER, false);
            }
            idx++;

            if ((pfds[idx].revents & POLLIN) != 0) {
                (void)accept_new_inet_connection(&state, state.listen_fd_admin,
                                                 clients, CLIENT_TYPE_ADMIN, true);
            }
            idx++;

            if ((pfds[idx].revents & POLLIN) != 0) {
                (void)accept_new_unix_connection(&state, clients);
            }
            idx++;

            if ((pfds[idx].revents & POLLIN) != 0) {
                handle_worker_pipe(&state);
            }
            idx++;

            if ((pfds[idx].revents & POLLIN) != 0) {
                handle_inotify_events(&state);
            }
            idx++;

            for (size_t i = 0U; i < MAX_CLIENTS; i++) {
                if (!clients[i].active) {
                    continue;
                }

                if ((pfds[idx].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
                    handle_client_command(&state, clients, &clients[i]);
                }
                idx++;
            }
        }

        for (size_t i = 0U; i < MAX_CLIENTS; i++) {
            if (clients[i].active && clients[i].kick_requested) {
                close_client(&clients[i]);
            }
        }
    }

    logger_log(LOG_LEVEL_INFO, "Stopping server");
    cleanup_server_state(&state, clients, workers_started, queue_initialized,
                         jobs_initialized, blocklist_initialized);
    logger_log(LOG_LEVEL_INFO, "Server stopped");
    return EXIT_SUCCESS;
}
