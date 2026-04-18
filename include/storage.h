#ifndef STORAGE_H
#define STORAGE_H

#include "common.h"

int ensure_directory_exists(const char *path);
int copy_file_to_storage(const char *src_path, char *dst_path, size_t dst_path_size);
int get_file_size_bytes(const char *path, off_t *size_out);

int storage_init_dirs(void);
int storage_make_upload_path(uint64_t job_id, const char *suffix, char *out, size_t out_size);
int storage_make_result_path(uint64_t job_id, const char *suffix, char *out, size_t out_size);
int storage_receive_to_file(int fd, size_t size, const char *dst_path);
int storage_send_file(int fd, const char *src_path);
int storage_read_all(const char *path, char *buffer, size_t buffer_size, size_t *out_len);

#endif
