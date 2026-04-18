#ifndef PNG_UTILS_H
#define PNG_UTILS_H

#include "common.h"
#include <png.h>

typedef struct png_metadata {
    png_uint_32 width;
    png_uint_32 height;
    int bit_depth;
    int color_type;
    int channels;
} png_metadata_t;

int is_png_signature_valid(const char *path);
int png_read_metadata(const char *path, png_metadata_t *metadata);
int png_calculate_lsb_capacity_bytes(const png_metadata_t *metadata, size_t *capacity_bytes);
int analyze_png_capacity_simple(const char *path, size_t *capacity_bytes);
const char *png_color_type_to_string(int color_type);

#endif
