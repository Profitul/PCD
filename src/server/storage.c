#include "config.h"
#include "net.h"
#include "storage.h"

int ensure_directory_exists(const char *path) {
    if (path == NULL) {
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

    if (mkdir(path, 0755) < 0) {
        return -1;
    }

    return 0;
}

int get_file_size_bytes(const char *path, off_t *size_out) {
    if (path == NULL || size_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    struct stat st;
    if (stat(path, &st) < 0) {
        return -1;
    }

    *size_out = st.st_size;
    return 0;
}

int storage_init_dirs(void) {
    if (ensure_directory_exists(STORAGE_ROOT) < 0) {
        return -1;
    }
    if (ensure_directory_exists(STORAGE_UPLOADS) < 0) {
        return -1;
    }
    if (ensure_directory_exists(STORAGE_RESULTS) < 0) {
        return -1;
    }
    if (ensure_directory_exists(STORAGE_TEMP) < 0) {
        return -1;
    }
    return 0;
}

int storage_make_upload_path(const uint64_t job_id, const char *suffix, char *out, const size_t out_size) {
    if (out == NULL || out_size == 0U) {
        return -1;
    }
    const char *s = (suffix != NULL) ? suffix : "bin";
    const int rc = snprintf(out, out_size, "%s/job_%llu.%s",
                            STORAGE_UPLOADS, (unsigned long long)job_id, s);
    if (rc < 0 || (size_t)rc >= out_size) {
        return -1;
    }
    return 0;
}

int storage_make_result_path(const uint64_t job_id, const char *suffix, char *out, const size_t out_size) {
    if (out == NULL || out_size == 0U) {
        return -1;
    }
    const char *s = (suffix != NULL) ? suffix : "bin";
    const int rc = snprintf(out, out_size, "%s/job_%llu.%s",
                            STORAGE_RESULTS, (unsigned long long)job_id, s);
    if (rc < 0 || (size_t)rc >= out_size) {
        return -1;
    }
    return 0;
}

int storage_receive_to_file(const int fd, const size_t size, const char *dst_path) {
    if (dst_path == NULL) {
        return -1;
    }

    FILE *fp = fopen(dst_path, "wb");
    if (fp == NULL) {
        return -1;
    }

    char buf[CHUNK_SIZE];
    size_t remaining = size;
    while (remaining > 0U) {
        const size_t want = (remaining < sizeof(buf)) ? remaining : sizeof(buf);
        const ssize_t rc = read(fd, buf, want);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            (void)fclose(fp);
            (void)unlink(dst_path);
            return -1;
        }
        if (rc == 0) {
            (void)fclose(fp);
            (void)unlink(dst_path);
            return -1;
        }
        if (fwrite(buf, 1U, (size_t)rc, fp) != (size_t)rc) {
            (void)fclose(fp);
            (void)unlink(dst_path);
            return -1;
        }
        remaining -= (size_t)rc;
    }

    if (fclose(fp) != 0) {
        (void)unlink(dst_path);
        return -1;
    }
    return 0;
}

int storage_send_file(const int fd, const char *src_path) {
    if (src_path == NULL) {
        return -1;
    }

    FILE *fp = fopen(src_path, "rb");
    if (fp == NULL) {
        return -1;
    }

    char buf[CHUNK_SIZE];
    while (1) {
        const size_t r = fread(buf, 1U, sizeof(buf), fp);
        if (r > 0U) {
            if (write_all(fd, buf, r) < 0) {
                (void)fclose(fp);
                return -1;
            }
        }
        if (r < sizeof(buf)) {
            if (ferror(fp) != 0) {
                (void)fclose(fp);
                return -1;
            }
            break;
        }
    }

    (void)fclose(fp);
    return 0;
}

int storage_read_all(const char *path, char *buffer, const size_t buffer_size, size_t *out_len) {
    if (path == NULL || buffer == NULL || buffer_size == 0U) {
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }

    const size_t r = fread(buffer, 1U, buffer_size - 1U, fp);
    buffer[r] = '\0';
    (void)fclose(fp);
    if (out_len != NULL) {
        *out_len = r;
    }
    return 0;
}

int copy_file_to_storage(const char *src_path, char *dst_path, const size_t dst_path_size) {
    if (src_path == NULL || dst_path == NULL || dst_path_size == 0U) {
        errno = EINVAL;
        return -1;
    }

    if (storage_init_dirs() < 0) {
        return -1;
    }

    const char *base = strrchr(src_path, '/');
    base = (base == NULL) ? src_path : (base + 1);

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
        return -1;
    }

    const int rc = snprintf(dst_path, dst_path_size, "%s/%ld_%ld_%s",
                            STORAGE_UPLOADS, (long)ts.tv_sec, (long)ts.tv_nsec, base);
    if (rc < 0 || (size_t)rc >= dst_path_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    FILE *src = fopen(src_path, "rb");
    if (src == NULL) {
        return -1;
    }

    FILE *dst = fopen(dst_path, "wb");
    if (dst == NULL) {
        (void)fclose(src);
        return -1;
    }

    char buffer[CHUNK_SIZE];
    int result = 0;

    while (1) {
        const size_t read_count = fread(buffer, 1U, sizeof(buffer), src);
        if (read_count > 0U) {
            if (fwrite(buffer, 1U, read_count, dst) != read_count) {
                result = -1;
                break;
            }
        }
        if (read_count < sizeof(buffer)) {
            if (ferror(src) != 0) {
                result = -1;
            }
            break;
        }
    }

    if (fclose(src) != 0) result = -1;
    if (fclose(dst) != 0) result = -1;

    if (result < 0) {
        (void)unlink(dst_path);
        return -1;
    }
    return 0;
}
