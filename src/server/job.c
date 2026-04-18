#include "job.h"

static void set_time_now(struct timespec *ts) {
    if (ts == NULL) {
        return;
    }
    (void)clock_gettime(CLOCK_REALTIME, ts);
}

static double timespec_diff_ms(const struct timespec *start, const struct timespec *end) {
    const double s = (double)(end->tv_sec - start->tv_sec) * 1000.0;
    const double ns = (double)(end->tv_nsec - start->tv_nsec) / 1.0e6;
    return s + ns;
}

int job_table_init(job_table_t *table) {
    if (table == NULL) {
        return -1;
    }
    (void)memset(table, 0, sizeof(*table));
    table->next_id = 1U;
    if (pthread_mutex_init(&table->mutex, NULL) != 0) {
        return -1;
    }
    return 0;
}

void job_table_destroy(job_table_t *table) {
    if (table == NULL) {
        return;
    }
    (void)pthread_mutex_lock(&table->mutex);
    for (size_t i = 0; i < table->count; i++) {
        if (table->jobs[i] != NULL) {
            (void)pthread_mutex_destroy(&table->jobs[i]->mutex);
            free(table->jobs[i]);
            table->jobs[i] = NULL;
        }
    }
    table->count = 0U;
    (void)pthread_mutex_unlock(&table->mutex);
    (void)pthread_mutex_destroy(&table->mutex);
}

job_t *job_table_create_job(job_table_t *table,
                            const int owner_fd,
                            const char *owner_ip,
                            const job_type_t type,
                            const char *payload,
                            const char *input_path) {
    if (table == NULL) {
        return NULL;
    }

    job_t *job = (job_t *)calloc(1U, sizeof(*job));
    if (job == NULL) {
        return NULL;
    }

    if (pthread_mutex_init(&job->mutex, NULL) != 0) {
        free(job);
        return NULL;
    }

    (void)pthread_mutex_lock(&table->mutex);
    if (table->count >= MAX_JOBS) {
        (void)pthread_mutex_unlock(&table->mutex);
        (void)pthread_mutex_destroy(&job->mutex);
        free(job);
        return NULL;
    }

    job->id = table->next_id;
    table->next_id++;
    job->owner_fd = owner_fd;
    job->type = type;
    job->state = JOB_STATE_QUEUED;
    job->cancel_requested = false;
    job->result_kind = JOB_RESULT_NONE;
    job->result_size = 0U;

    if (owner_ip != NULL) {
        (void)snprintf(job->owner_ip, sizeof(job->owner_ip), "%s", owner_ip);
    }
    if (payload != NULL) {
        (void)snprintf(job->payload, sizeof(job->payload), "%s", payload);
    }
    if (input_path != NULL) {
        (void)snprintf(job->input_path, sizeof(job->input_path), "%s", input_path);
    }

    set_time_now(&job->created_at);

    table->jobs[table->count] = job;
    table->count++;
    (void)pthread_mutex_unlock(&table->mutex);
    return job;
}

job_t *job_table_find(job_table_t *table, const uint64_t job_id) {
    if (table == NULL || job_id == 0U) {
        return NULL;
    }
    job_t *result = NULL;
    (void)pthread_mutex_lock(&table->mutex);
    for (size_t i = 0; i < table->count; i++) {
        if (table->jobs[i] != NULL && table->jobs[i]->id == job_id) {
            result = table->jobs[i];
            break;
        }
    }
    (void)pthread_mutex_unlock(&table->mutex);
    return result;
}

const char *job_state_to_string(const job_state_t state) {
    switch (state) {
        case JOB_STATE_QUEUED:   return "QUEUED";
        case JOB_STATE_RUNNING:  return "RUNNING";
        case JOB_STATE_DONE:     return "DONE";
        case JOB_STATE_FAILED:   return "FAILED";
        case JOB_STATE_CANCELED: return "CANCELED";
        default:                 return "UNKNOWN";
    }
}

const char *job_type_to_string(const job_type_t type) {
    switch (type) {
        case JOB_TYPE_TEXT:             return "TEXT";
        case JOB_TYPE_VALIDATE_IMAGE:   return "VALIDATE_IMAGE";
        case JOB_TYPE_ANALYZE_CAPACITY: return "ANALYZE_CAPACITY";
        case JOB_TYPE_ENCODE_TEXT:      return "ENCODE_TEXT";
        case JOB_TYPE_ENCODE_FILE:      return "ENCODE_FILE";
        case JOB_TYPE_DECODE:           return "DECODE";
        default:                        return "UNKNOWN";
    }
}

const char *job_result_kind_to_string(const job_result_kind_t kind) {
    switch (kind) {
        case JOB_RESULT_TEXT: return "TEXT";
        case JOB_RESULT_FILE: return "FILE";
        case JOB_RESULT_PNG:  return "PNG";
        default:              return "NONE";
    }
}

bool job_request_cancel(job_t *job) {
    if (job == NULL) return false;
    bool ok = false;
    (void)pthread_mutex_lock(&job->mutex);
    if (job->state == JOB_STATE_QUEUED || job->state == JOB_STATE_RUNNING) {
        job->cancel_requested = true;
        ok = true;
    }
    (void)pthread_mutex_unlock(&job->mutex);
    return ok;
}

void job_set_state(job_t *job, const job_state_t state) {
    if (job == NULL) return;
    (void)pthread_mutex_lock(&job->mutex);
    job->state = state;
    if (state == JOB_STATE_RUNNING) {
        set_time_now(&job->started_at);
    }
    if (state == JOB_STATE_DONE || state == JOB_STATE_FAILED || state == JOB_STATE_CANCELED) {
        set_time_now(&job->finished_at);
    }
    (void)pthread_mutex_unlock(&job->mutex);
}

job_state_t job_get_state(job_t *job) {
    if (job == NULL) return JOB_STATE_FAILED;
    job_state_t state;
    (void)pthread_mutex_lock(&job->mutex);
    state = job->state;
    (void)pthread_mutex_unlock(&job->mutex);
    return state;
}

bool job_is_cancel_requested(job_t *job) {
    if (job == NULL) return true;
    bool requested;
    (void)pthread_mutex_lock(&job->mutex);
    requested = job->cancel_requested;
    (void)pthread_mutex_unlock(&job->mutex);
    return requested;
}

void job_set_result(job_t *job, const char *result) {
    if (job == NULL || result == NULL) return;
    (void)pthread_mutex_lock(&job->mutex);
    (void)snprintf(job->result, sizeof(job->result), "%s", result);
    (void)pthread_mutex_unlock(&job->mutex);
}

void job_get_result(job_t *job, char *buffer, const size_t buffer_size) {
    if (job == NULL || buffer == NULL || buffer_size == 0U) return;
    (void)pthread_mutex_lock(&job->mutex);
    (void)snprintf(buffer, buffer_size, "%s", job->result);
    (void)pthread_mutex_unlock(&job->mutex);
}

void job_get_payload(job_t *job, char *buffer, const size_t buffer_size) {
    if (job == NULL || buffer == NULL || buffer_size == 0U) return;
    (void)pthread_mutex_lock(&job->mutex);
    (void)snprintf(buffer, buffer_size, "%s", job->payload);
    (void)pthread_mutex_unlock(&job->mutex);
}

void job_get_input_path(job_t *job, char *buffer, const size_t buffer_size) {
    if (job == NULL || buffer == NULL || buffer_size == 0U) return;
    (void)pthread_mutex_lock(&job->mutex);
    (void)snprintf(buffer, buffer_size, "%s", job->input_path);
    (void)pthread_mutex_unlock(&job->mutex);
}

void job_set_stored_path(job_t *job, const char *path) {
    if (job == NULL || path == NULL) return;
    (void)pthread_mutex_lock(&job->mutex);
    (void)snprintf(job->stored_path, sizeof(job->stored_path), "%s", path);
    (void)pthread_mutex_unlock(&job->mutex);
}

void job_get_stored_path(job_t *job, char *buffer, const size_t buffer_size) {
    if (job == NULL || buffer == NULL || buffer_size == 0U) return;
    (void)pthread_mutex_lock(&job->mutex);
    (void)snprintf(buffer, buffer_size, "%s", job->stored_path);
    (void)pthread_mutex_unlock(&job->mutex);
}

void job_set_output_path(job_t *job, const char *path) {
    if (job == NULL || path == NULL) return;
    (void)pthread_mutex_lock(&job->mutex);
    (void)snprintf(job->output_path, sizeof(job->output_path), "%s", path);
    (void)pthread_mutex_unlock(&job->mutex);
}

void job_get_output_path(job_t *job, char *buffer, const size_t buffer_size) {
    if (job == NULL || buffer == NULL || buffer_size == 0U) return;
    (void)pthread_mutex_lock(&job->mutex);
    (void)snprintf(buffer, buffer_size, "%s", job->output_path);
    (void)pthread_mutex_unlock(&job->mutex);
}

void job_set_result_meta(job_t *job, const job_result_kind_t kind, const size_t size, const char *filename) {
    if (job == NULL) return;
    (void)pthread_mutex_lock(&job->mutex);
    job->result_kind = kind;
    job->result_size = size;
    if (filename != NULL) {
        (void)snprintf(job->result_filename, sizeof(job->result_filename), "%s", filename);
    } else {
        job->result_filename[0] = '\0';
    }
    (void)pthread_mutex_unlock(&job->mutex);
}

void job_get_result_meta(job_t *job, job_result_kind_t *kind, size_t *size, char *filename, const size_t filename_size) {
    if (job == NULL) return;
    (void)pthread_mutex_lock(&job->mutex);
    if (kind != NULL) *kind = job->result_kind;
    if (size != NULL) *size = job->result_size;
    if (filename != NULL && filename_size > 0U) {
        (void)snprintf(filename, filename_size, "%s", job->result_filename);
    }
    (void)pthread_mutex_unlock(&job->mutex);
}

int job_get_owner_fd(job_t *job) {
    if (job == NULL) return -1;
    int fd;
    (void)pthread_mutex_lock(&job->mutex);
    fd = job->owner_fd;
    (void)pthread_mutex_unlock(&job->mutex);
    return fd;
}

void job_get_owner_ip(job_t *job, char *buffer, const size_t buffer_size) {
    if (job == NULL || buffer == NULL || buffer_size == 0U) return;
    (void)pthread_mutex_lock(&job->mutex);
    (void)snprintf(buffer, buffer_size, "%s", job->owner_ip);
    (void)pthread_mutex_unlock(&job->mutex);
}

job_type_t job_get_type(job_t *job) {
    if (job == NULL) return JOB_TYPE_UNKNOWN;
    job_type_t type;
    (void)pthread_mutex_lock(&job->mutex);
    type = job->type;
    (void)pthread_mutex_unlock(&job->mutex);
    return type;
}

void job_table_collect_stats(job_table_t *table, job_stats_t *stats) {
    if (table == NULL || stats == NULL) return;
    (void)memset(stats, 0, sizeof(*stats));

    double total_duration = 0.0;
    size_t finished_count = 0U;

    (void)pthread_mutex_lock(&table->mutex);
    stats->total = table->count;

    for (size_t i = 0; i < table->count; i++) {
        if (table->jobs[i] == NULL) continue;
        (void)pthread_mutex_lock(&table->jobs[i]->mutex);
        switch (table->jobs[i]->state) {
            case JOB_STATE_QUEUED:   stats->queued++; break;
            case JOB_STATE_RUNNING:  stats->running++; break;
            case JOB_STATE_DONE:     stats->done++; break;
            case JOB_STATE_FAILED:   stats->failed++; break;
            case JOB_STATE_CANCELED: stats->canceled++; break;
            default: break;
        }
        if (table->jobs[i]->state == JOB_STATE_DONE || table->jobs[i]->state == JOB_STATE_FAILED) {
            const double d = timespec_diff_ms(&table->jobs[i]->started_at, &table->jobs[i]->finished_at);
            if (d >= 0.0) {
                total_duration += d;
                finished_count++;
            }
        }
        (void)pthread_mutex_unlock(&table->jobs[i]->mutex);
    }
    (void)pthread_mutex_unlock(&table->mutex);

    stats->avg_duration_ms = (finished_count > 0U) ? (total_duration / (double)finished_count) : 0.0;
}

int job_table_format_list(job_table_t *table, char *buffer, const size_t buffer_size) {
    if (table == NULL || buffer == NULL || buffer_size == 0U) return -1;
    buffer[0] = '\0';
    size_t used = 0U;

    (void)pthread_mutex_lock(&table->mutex);
    if (table->count == 0U) {
        (void)snprintf(buffer, buffer_size, "empty");
        (void)pthread_mutex_unlock(&table->mutex);
        return 0;
    }

    for (size_t i = 0; i < table->count; i++) {
        if (table->jobs[i] == NULL) continue;
        char item[256];
        (void)pthread_mutex_lock(&table->jobs[i]->mutex);
        (void)snprintf(item, sizeof(item), "id=%llu,type=%s,state=%s,ip=%s",
                       (unsigned long long)table->jobs[i]->id,
                       job_type_to_string(table->jobs[i]->type),
                       job_state_to_string(table->jobs[i]->state),
                       table->jobs[i]->owner_ip);
        (void)pthread_mutex_unlock(&table->jobs[i]->mutex);

        const size_t item_len = strlen(item);
        const size_t extra_sep = (used == 0U) ? 0U : 1U;
        if (used + extra_sep + item_len + 1U >= buffer_size) break;
        if (used != 0U) {
            buffer[used] = ';'; used++; buffer[used] = '\0';
        }
        (void)snprintf(buffer + used, buffer_size - used, "%s", item);
        used += item_len;
    }
    (void)pthread_mutex_unlock(&table->mutex);
    return 0;
}

int job_table_format_history(job_table_t *table, char *buffer, const size_t buffer_size, const size_t max_items) {
    if (table == NULL || buffer == NULL || buffer_size == 0U) return -1;
    buffer[0] = '\0';
    size_t used = 0U;
    size_t emitted = 0U;

    (void)pthread_mutex_lock(&table->mutex);
    const size_t n = table->count;
    for (size_t i = n; i > 0U && emitted < max_items; i--) {
        job_t *job = table->jobs[i - 1U];
        if (job == NULL) continue;
        (void)pthread_mutex_lock(&job->mutex);
        if (job->state == JOB_STATE_DONE || job->state == JOB_STATE_FAILED || job->state == JOB_STATE_CANCELED) {
            const double d = timespec_diff_ms(&job->started_at, &job->finished_at);
            char item[320];
            (void)snprintf(item, sizeof(item),
                           "id=%llu,type=%s,state=%s,dur_ms=%.1f,ip=%s",
                           (unsigned long long)job->id,
                           job_type_to_string(job->type),
                           job_state_to_string(job->state),
                           d,
                           job->owner_ip);
            const size_t item_len = strlen(item);
            const size_t extra_sep = (used == 0U) ? 0U : 1U;
            if (used + extra_sep + item_len + 1U < buffer_size) {
                if (used != 0U) { buffer[used] = ';'; used++; buffer[used] = '\0'; }
                (void)snprintf(buffer + used, buffer_size - used, "%s", item);
                used += item_len;
                emitted++;
            }
        }
        (void)pthread_mutex_unlock(&job->mutex);
    }
    if (emitted == 0U) {
        (void)snprintf(buffer, buffer_size, "empty");
    }
    (void)pthread_mutex_unlock(&table->mutex);
    return 0;
}

int job_table_cancel_by_owner(job_table_t *table, const int owner_fd) {
    if (table == NULL) return 0;
    int count = 0;
    (void)pthread_mutex_lock(&table->mutex);
    for (size_t i = 0; i < table->count; i++) {
        job_t *job = table->jobs[i];
        if (job == NULL) continue;
        (void)pthread_mutex_lock(&job->mutex);
        if (job->owner_fd == owner_fd &&
            (job->state == JOB_STATE_QUEUED || job->state == JOB_STATE_RUNNING)) {
            job->cancel_requested = true;
            count++;
        }
        (void)pthread_mutex_unlock(&job->mutex);
    }
    (void)pthread_mutex_unlock(&table->mutex);
    return count;
}
