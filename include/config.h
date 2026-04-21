#ifndef CONFIG_H
#define CONFIG_H

#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT 9090
#define ADMIN_PORT 9091

#define MAX_CLIENTS 128
#define MAX_POLL_FDS 256
#define BUFFER_SIZE 4096
#define SMALL_BUFFER_SIZE 256
#define LOG_PATH "logs/server.log"

#define MAX_WORKERS 8
#define DEFAULT_WORKER_COUNT 3

#define MAX_UPLOAD_BYTES (64ULL * 1024ULL * 1024ULL)
#define MAX_TEXT_BYTES   (1ULL  * 1024ULL * 1024ULL)
#define MAX_FILENAME_BYTES 255U

#define STORAGE_ROOT     "storage"
#define STORAGE_UPLOADS  "storage/uploads"
#define STORAGE_RESULTS  "storage/results"
#define STORAGE_TEMP     "storage/temp"

#define DEFAULT_CONFIG_PATH "config/server.conf"
#define CHUNK_SIZE 65536

#endif
