#ifndef JOB_H
#define JOB_H

#include "common.h"

#define MAX_JOBS 1024U
#define MAX_JOB_PAYLOAD 1024U
#define MAX_JOB_RESULT 2048U
#define MAX_JOB_PATH 512U
#define MAX_JOB_FILENAME 256U

typedef enum job_type {
    JOB_TYPE_UNKNOWN = 0,
    JOB_TYPE_TEXT = 1,
    JOB_TYPE_VALIDATE_IMAGE = 2,
    JOB_TYPE_ANALYZE_CAPACITY = 3,
    JOB_TYPE_ENCODE_TEXT = 4,
    JOB_TYPE_ENCODE_FILE = 5,
    JOB_TYPE_DECODE = 6
} job_type_t;

typedef enum job_state {
    JOB_STATE_QUEUED = 0,
    JOB_STATE_RUNNING = 1,
    JOB_STATE_DONE = 2,
    JOB_STATE_FAILED = 3,
    JOB_STATE_CANCELED = 4
} job_state_t;

typedef enum job_result_kind {
    JOB_RESULT_NONE = 0,
    JOB_RESULT_TEXT = 1,
    JOB_RESULT_FILE = 2,
    JOB_RESULT_PNG  = 3
} job_result_kind_t;

typedef struct job {
    uint64_t id;
    int owner_fd;
    job_type_t type;
    job_state_t state;
    bool cancel_requested;
    char payload[MAX_JOB_PAYLOAD];
    char input_path[MAX_JOB_PATH];
    char stored_path[MAX_JOB_PATH];
    char output_path[MAX_JOB_PATH];
    char result_filename[MAX_JOB_FILENAME];
    job_result_kind_t result_kind;
    size_t result_size;
    char result[MAX_JOB_RESULT];
    char owner_ip[64];
    struct timespec created_at;
    struct timespec started_at;
    struct timespec finished_at;
    pthread_mutex_t mutex;
} job_t;

typedef struct job_table {
    job_t *jobs[MAX_JOBS];
    size_t count;
    uint64_t next_id;
    pthread_mutex_t mutex;
} job_table_t;

typedef struct job_stats {
    size_t total;
    size_t queued;
    size_t running;
    size_t done;
    size_t failed;
    size_t canceled;
    double avg_duration_ms;
} job_stats_t;

int job_table_init(job_table_t *table);
void job_table_destroy(job_table_t *table);

job_t *job_table_create_job(job_table_t *table,
                            int owner_fd,
                            const char *owner_ip,
                            job_type_t type,
                            const char *payload,
                            const char *input_path);

job_t *job_table_find(job_table_t *table, uint64_t job_id);

const char *job_state_to_string(job_state_t state);
const char *job_type_to_string(job_type_t type);
const char *job_result_kind_to_string(job_result_kind_t kind);

bool job_request_cancel(job_t *job);
void job_set_state(job_t *job, job_state_t state);
job_state_t job_get_state(job_t *job);
bool job_is_cancel_requested(job_t *job);

void job_set_result(job_t *job, const char *result);
void job_get_result(job_t *job, char *buffer, size_t buffer_size);
void job_get_payload(job_t *job, char *buffer, size_t buffer_size);
void job_get_input_path(job_t *job, char *buffer, size_t buffer_size);
void job_set_stored_path(job_t *job, const char *path);
void job_get_stored_path(job_t *job, char *buffer, size_t buffer_size);
void job_set_output_path(job_t *job, const char *path);
void job_get_output_path(job_t *job, char *buffer, size_t buffer_size);
void job_set_result_meta(job_t *job, job_result_kind_t kind, size_t size, const char *filename);
void job_get_result_meta(job_t *job, job_result_kind_t *kind, size_t *size, char *filename, size_t filename_size);
int job_get_owner_fd(job_t *job);
void job_get_owner_ip(job_t *job, char *buffer, size_t buffer_size);
job_type_t job_get_type(job_t *job);

void job_table_collect_stats(job_table_t *table, job_stats_t *stats);
int job_table_format_list(job_table_t *table, char *buffer, size_t buffer_size);
int job_table_format_history(job_table_t *table, char *buffer, size_t buffer_size, size_t max_items);
int job_table_cancel_by_owner(job_table_t *table, int owner_fd);

#endif
