#ifndef STEGO_H
#define STEGO_H

#include "common.h"

#define STEGO_MAGIC "STG1"
#define STEGO_MAGIC_LEN 4U
#define STEGO_HEADER_MIN_LEN 15U

typedef enum stego_payload_type {
    STEGO_PAYLOAD_TEXT = 0,
    STEGO_PAYLOAD_FILE = 1
} stego_payload_type_t;

typedef enum stego_status {
    STEGO_OK = 0,
    STEGO_ERR_OPEN = -1,
    STEGO_ERR_NOT_PNG = -2,
    STEGO_ERR_UNSUPPORTED = -3,
    STEGO_ERR_MEMORY = -4,
    STEGO_ERR_CAPACITY = -5,
    STEGO_ERR_LIBPNG = -6,
    STEGO_ERR_MAGIC = -7,
    STEGO_ERR_CRC = -8,
    STEGO_ERR_IO = -9,
    STEGO_ERR_ARG = -10
} stego_status_t;

typedef struct stego_capacity {
    size_t payload_max_bytes;
    uint32_t width;
    uint32_t height;
    int channels;
    int data_channels;
} stego_capacity_t;

typedef struct stego_extracted {
    stego_payload_type_t type;
    char filename[256];
    uint8_t *data;
    size_t data_len;
} stego_extracted_t;

const char *stego_strerror(stego_status_t status);

int stego_get_capacity(const char *png_path, stego_capacity_t *out);

int stego_encode_text(const char *input_png_path,
                      const char *output_png_path,
                      const char *text,
                      size_t text_len);

int stego_encode_file(const char *input_png_path,
                      const char *output_png_path,
                      const char *payload_file_path,
                      const char *store_as_filename);

int stego_decode(const char *png_path, stego_extracted_t *out);

void stego_extracted_free(stego_extracted_t *e);

#endif
