#include "config.h"
#include "logger.h"
#include "png_utils.h"
#include "stego.h"
#include "storage.h"
#include "worker.h"

static int write_event(const int fd, const worker_event_t *event) {
    if (event == NULL) {
        return -1;
    }
    size_t total = 0U;
    const unsigned char *ptr = (const unsigned char *)event;
    while (total < sizeof(*event)) {
        const ssize_t rc = write(fd, ptr + total, sizeof(*event) - total);
        if (rc < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (rc == 0) return -1;
        total += (size_t)rc;
    }
    return 0;
}

static int write_text_to_file(const char *path, const char *text, size_t len) {
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) return -1;
    if (len > 0U && fwrite(text, 1U, len, fp) != len) {
        (void)fclose(fp);
        return -1;
    }
    if (fclose(fp) != 0) return -1;
    return 0;
}

static int write_bin_to_file(const char *path, const uint8_t *data, size_t len) {
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) return -1;
    if (len > 0U && fwrite(data, 1U, len, fp) != len) {
        (void)fclose(fp);
        return -1;
    }
    if (fclose(fp) != 0) return -1;
    return 0;
}

static int read_text_file(const char *path, char *buf, size_t buf_size, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) return -1;
    const size_t r = fread(buf, 1U, buf_size - 1U, fp);
    buf[r] = '\0';
    (void)fclose(fp);
    if (out_len != NULL) *out_len = r;
    return 0;
}

static int process_text_job(worker_context_t *ctx, job_t *job) {
    char payload[MAX_JOB_PAYLOAD];
    job_get_payload(job, payload, sizeof(payload));

    logger_log(LOG_LEVEL_INFO,
               "Worker %zu picked TEXT job_id=%llu payload=\"%s\"",
               ctx->worker_index, (unsigned long long)job->id, payload);

    for (size_t step = 0U; step < 5U; step++) {
        struct timespec req;
        req.tv_sec = 0;
        req.tv_nsec = 100000000L;
        (void)nanosleep(&req, NULL);
        if (job_is_cancel_requested(job)) {
            job_set_result(job, "job canceled");
            job_set_state(job, JOB_STATE_CANCELED);
            return 0;
        }
    }

    char final_result[MAX_JOB_RESULT];
    (void)snprintf(final_result, sizeof(final_result),
                   "processed_by_worker_%zu: %s", ctx->worker_index, payload);
    job_set_result(job, final_result);
    job_set_state(job, JOB_STATE_DONE);
    return 0;
}

static int process_validate_image_job(worker_context_t *ctx, job_t *job) {
    char input_path[MAX_JOB_PATH];
    job_get_input_path(job, input_path, sizeof(input_path));
    logger_log(LOG_LEVEL_INFO, "Worker %zu VALIDATE job_id=%llu path=\"%s\"",
               ctx->worker_index, (unsigned long long)job->id, input_path);

    png_metadata_t metadata;
    if (png_read_metadata(input_path, &metadata) < 0) {
        job_set_result(job, "INVALID PNG");
        job_set_state(job, JOB_STATE_FAILED);
        return -1;
    }

    char result[MAX_JOB_RESULT];
    (void)snprintf(result, sizeof(result),
                   "VALID PNG width=%u height=%u bit_depth=%d color=%s channels=%d",
                   metadata.width, metadata.height, metadata.bit_depth,
                   png_color_type_to_string(metadata.color_type), metadata.channels);
    job_set_result(job, result);
    job_set_state(job, JOB_STATE_DONE);
    return 0;
}

static int process_analyze_capacity_job(worker_context_t *ctx, job_t *job) {
    char input_path[MAX_JOB_PATH];
    job_get_input_path(job, input_path, sizeof(input_path));
    logger_log(LOG_LEVEL_INFO, "Worker %zu CAPACITY job_id=%llu path=\"%s\"",
               ctx->worker_index, (unsigned long long)job->id, input_path);

    stego_capacity_t cap;
    const int rc = stego_get_capacity(input_path, &cap);
    if (rc != STEGO_OK) {
        char msg[MAX_JOB_RESULT];
        (void)snprintf(msg, sizeof(msg), "capacity error: %s", stego_strerror(rc));
        job_set_result(job, msg);
        job_set_state(job, JOB_STATE_FAILED);
        return -1;
    }

    char result[MAX_JOB_RESULT];
    (void)snprintf(result, sizeof(result),
                   "CAPACITY %zu BYTES width=%u height=%u channels=%d",
                   cap.payload_max_bytes, cap.width, cap.height, cap.channels);
    job_set_result(job, result);
    job_set_state(job, JOB_STATE_DONE);
    return 0;
}

static int process_encode_text_job(worker_context_t *ctx, job_t *job) {
    char png_path[MAX_JOB_PATH];
    char text_path[MAX_JOB_PATH];
    char out_path[MAX_JOB_PATH];

    job_get_input_path(job, png_path, sizeof(png_path));
    job_get_stored_path(job, text_path, sizeof(text_path));

    if (storage_make_result_path(job->id, "png", out_path, sizeof(out_path)) < 0) {
        job_set_result(job, "path build failed");
        job_set_state(job, JOB_STATE_FAILED);
        return -1;
    }

    logger_log(LOG_LEVEL_INFO, "Worker %zu ENCODE_TEXT job_id=%llu png=%s text=%s",
               ctx->worker_index, (unsigned long long)job->id, png_path, text_path);

    char text_buf[MAX_TEXT_BYTES + 1U];
    size_t text_len = 0U;
    if (read_text_file(text_path, text_buf, sizeof(text_buf), &text_len) < 0) {
        job_set_result(job, "cannot read text payload");
        job_set_state(job, JOB_STATE_FAILED);
        return -1;
    }

    if (job_is_cancel_requested(job)) {
        job_set_result(job, "job canceled");
        job_set_state(job, JOB_STATE_CANCELED);
        return 0;
    }

    const int rc = stego_encode_text(png_path, out_path, text_buf, text_len);
    if (rc != STEGO_OK) {
        char msg[MAX_JOB_RESULT];
        (void)snprintf(msg, sizeof(msg), "encode_text failed: %s", stego_strerror(rc));
        job_set_result(job, msg);
        job_set_state(job, JOB_STATE_FAILED);
        return -1;
    }

    off_t size = 0;
    (void)get_file_size_bytes(out_path, &size);
    job_set_output_path(job, out_path);
    job_set_result_meta(job, JOB_RESULT_PNG, (size_t)size, "stego_result.png");

    char msg[MAX_JOB_RESULT];
    (void)snprintf(msg, sizeof(msg),
                   "encoded text_bytes=%zu output=%s size=%lld",
                   text_len, out_path, (long long)size);
    job_set_result(job, msg);
    job_set_state(job, JOB_STATE_DONE);
    return 0;
}

static int process_encode_file_job(worker_context_t *ctx, job_t *job) {
    char png_path[MAX_JOB_PATH];
    char file_path[MAX_JOB_PATH];
    char out_path[MAX_JOB_PATH];
    char filename[MAX_JOB_FILENAME];

    job_get_input_path(job, png_path, sizeof(png_path));
    job_get_stored_path(job, file_path, sizeof(file_path));
    job_get_payload(job, filename, sizeof(filename));

    if (storage_make_result_path(job->id, "png", out_path, sizeof(out_path)) < 0) {
        job_set_result(job, "path build failed");
        job_set_state(job, JOB_STATE_FAILED);
        return -1;
    }

    logger_log(LOG_LEVEL_INFO, "Worker %zu ENCODE_FILE job_id=%llu png=%s file=%s name=%s",
               ctx->worker_index, (unsigned long long)job->id, png_path, file_path, filename);

    if (job_is_cancel_requested(job)) {
        job_set_result(job, "job canceled");
        job_set_state(job, JOB_STATE_CANCELED);
        return 0;
    }

    const int rc = stego_encode_file(png_path, out_path, file_path,
                                     (filename[0] != '\0') ? filename : NULL);
    if (rc != STEGO_OK) {
        char msg[MAX_JOB_RESULT];
        (void)snprintf(msg, sizeof(msg), "encode_file failed: %s", stego_strerror(rc));
        job_set_result(job, msg);
        job_set_state(job, JOB_STATE_FAILED);
        return -1;
    }

    off_t size = 0;
    (void)get_file_size_bytes(out_path, &size);
    job_set_output_path(job, out_path);
    job_set_result_meta(job, JOB_RESULT_PNG, (size_t)size, "stego_result.png");

    char msg[MAX_JOB_RESULT];
    (void)snprintf(msg, sizeof(msg),
                   "encoded file=%s output=%s size=%lld",
                   filename, out_path, (long long)size);
    job_set_result(job, msg);
    job_set_state(job, JOB_STATE_DONE);
    return 0;
}

static int process_decode_job(worker_context_t *ctx, job_t *job) {
    char png_path[MAX_JOB_PATH];
    job_get_input_path(job, png_path, sizeof(png_path));

    logger_log(LOG_LEVEL_INFO, "Worker %zu DECODE job_id=%llu png=%s",
               ctx->worker_index, (unsigned long long)job->id, png_path);

    if (job_is_cancel_requested(job)) {
        job_set_result(job, "job canceled");
        job_set_state(job, JOB_STATE_CANCELED);
        return 0;
    }

    stego_extracted_t ex;
    const int rc = stego_decode(png_path, &ex);
    if (rc != STEGO_OK) {
        char msg[MAX_JOB_RESULT];
        (void)snprintf(msg, sizeof(msg), "decode failed: %s", stego_strerror(rc));
        job_set_result(job, msg);
        job_set_state(job, JOB_STATE_FAILED);
        return -1;
    }

    char out_path[MAX_JOB_PATH];
    const char *suffix = (ex.type == STEGO_PAYLOAD_TEXT) ? "txt" : "bin";
    if (storage_make_result_path(job->id, suffix, out_path, sizeof(out_path)) < 0) {
        stego_extracted_free(&ex);
        job_set_result(job, "path build failed");
        job_set_state(job, JOB_STATE_FAILED);
        return -1;
    }

    int wr;
    if (ex.type == STEGO_PAYLOAD_TEXT) {
        wr = write_text_to_file(out_path, (const char *)ex.data, ex.data_len);
    } else {
        wr = write_bin_to_file(out_path, ex.data, ex.data_len);
    }
    if (wr < 0) {
        stego_extracted_free(&ex);
        job_set_result(job, "cannot write decoded result");
        job_set_state(job, JOB_STATE_FAILED);
        return -1;
    }

    job_set_output_path(job, out_path);
    const job_result_kind_t kind = (ex.type == STEGO_PAYLOAD_TEXT) ? JOB_RESULT_TEXT : JOB_RESULT_FILE;
    const char *name = (ex.type == STEGO_PAYLOAD_TEXT) ? "decoded.txt" : ex.filename;
    job_set_result_meta(job, kind, ex.data_len, name);

    char msg[MAX_JOB_RESULT];
    if (ex.type == STEGO_PAYLOAD_TEXT) {
        (void)snprintf(msg, sizeof(msg), "decoded text bytes=%zu", ex.data_len);
    } else {
        (void)snprintf(msg, sizeof(msg), "decoded file=%s bytes=%zu", ex.filename, ex.data_len);
    }
    job_set_result(job, msg);
    job_set_state(job, JOB_STATE_DONE);

    stego_extracted_free(&ex);
    return 0;
}

void *worker_main(void *arg) {
    worker_context_t *ctx = (worker_context_t *)arg;
    if (ctx == NULL) {
        return NULL;
    }

    logger_log(LOG_LEVEL_INFO, "Worker %zu started", ctx->worker_index);

    while (*(ctx->running) != 0) {
        job_t *job = job_queue_pop(ctx->queue);
        if (job == NULL) break;

        if (job_is_cancel_requested(job)) {
            job_set_result(job, "job canceled");
            job_set_state(job, JOB_STATE_CANCELED);
            worker_event_t event;
            event.job_id = job->id;
            event.state = JOB_STATE_CANCELED;
            (void)write_event(ctx->notify_fd, &event);
            continue;
        }

        job_set_state(job, JOB_STATE_RUNNING);

        int rc = -1;
        const job_type_t type = job_get_type(job);

        switch (type) {
            case JOB_TYPE_TEXT:             rc = process_text_job(ctx, job); break;
            case JOB_TYPE_VALIDATE_IMAGE:   rc = process_validate_image_job(ctx, job); break;
            case JOB_TYPE_ANALYZE_CAPACITY: rc = process_analyze_capacity_job(ctx, job); break;
            case JOB_TYPE_ENCODE_TEXT:      rc = process_encode_text_job(ctx, job); break;
            case JOB_TYPE_ENCODE_FILE:      rc = process_encode_file_job(ctx, job); break;
            case JOB_TYPE_DECODE:           rc = process_decode_job(ctx, job); break;
            default:
                job_set_result(job, "unknown job type");
                job_set_state(job, JOB_STATE_FAILED);
                rc = -1;
                break;
        }

        if (rc < 0 && job_get_state(job) == JOB_STATE_RUNNING) {
            job_set_result(job, "worker failed");
            job_set_state(job, JOB_STATE_FAILED);
        }

        worker_event_t event;
        event.job_id = job->id;
        event.state = job_get_state(job);
        (void)write_event(ctx->notify_fd, &event);
    }

    logger_log(LOG_LEVEL_INFO, "Worker %zu stopped", ctx->worker_index);
    return NULL;
}
