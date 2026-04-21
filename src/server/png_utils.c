#include "png_utils.h"

/* Verifica daca primii 8 bytes ai fisierului corespund signaturii PNG.
   Returneaza 1 (valid), 0 (invalid), -1 (eroare I/O). */
int is_png_signature_valid(const char *path) {
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }

    png_byte sig[8];
    const size_t nread = fread(sig, 1U, sizeof(sig), fp);
    (void)fclose(fp);

    if (nread != sizeof(sig)) {
        return 0;
    }

    return (png_sig_cmp(sig, 0, (int)sizeof(sig)) == 0) ? 1 : 0;
}

/* Citeste metadatele unui PNG (dimensiuni, bit_depth, color_type, channels)
   fara a decoda pixelii imaginii. */
int png_read_metadata(const char *path, png_metadata_t *metadata) {
    if (path == NULL || metadata == NULL) {
        errno = EINVAL;
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }

    png_byte header[8];
    if (fread(header, 1U, sizeof(header), fp) != sizeof(header)) {
        (void)fclose(fp);
        errno = EIO;
        return -1;
    }

    if (png_sig_cmp(header, 0, (int)sizeof(header)) != 0) {
        (void)fclose(fp);
        errno = EINVAL;
        return -1;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) {
        (void)fclose(fp);
        errno = ENOMEM;
        return -1;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        (void)fclose(fp);
        errno = ENOMEM;
        return -1;
    }

    if (setjmp(png_jmpbuf(png_ptr)) != 0) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        (void)fclose(fp);
        errno = EINVAL;
        return -1;
    }

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, (int)sizeof(header));
    png_read_info(png_ptr, info_ptr);

    metadata->width = png_get_image_width(png_ptr, info_ptr);
    metadata->height = png_get_image_height(png_ptr, info_ptr);
    metadata->bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    metadata->color_type = png_get_color_type(png_ptr, info_ptr);
    metadata->channels = png_get_channels(png_ptr, info_ptr);

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    (void)fclose(fp);
    return 0;
}

/* Calculeaza capacitatea LSB in bytes dintr-un set de metadate PNG.
   Suporta doar imagini cu bit_depth == 8; rezerva 16 bytes pentru overhead. */
int png_calculate_lsb_capacity_bytes(const png_metadata_t *metadata, size_t *capacity_bytes) {
    if (metadata == NULL || capacity_bytes == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (metadata->width == 0U || metadata->height == 0U || metadata->channels <= 0) {
        errno = EINVAL;
        return -1;
    }

    if (metadata->bit_depth != 8) {
        errno = ENOTSUP;
        return -1;
    }

    const unsigned long long pixels =
        (unsigned long long)metadata->width * (unsigned long long)metadata->height;
    const unsigned long long usable_bits = pixels * (unsigned long long)metadata->channels;
    const unsigned long long reserved_bytes = 16ULL;
    const unsigned long long total_bytes = usable_bits / 8ULL;

    *capacity_bytes = (total_bytes > reserved_bytes) ? (size_t)(total_bytes - reserved_bytes) : 0U;
    return 0;
}

/* Wrapper: citeste metadatele din fisier si calculeaza capacitatea LSB in un singur apel. */
int analyze_png_capacity_simple(const char *path, size_t *capacity_bytes) {
    if (path == NULL || capacity_bytes == NULL) {
        errno = EINVAL;
        return -1;
    }

    png_metadata_t metadata;
    if (png_read_metadata(path, &metadata) < 0) {
        return -1;
    }

    return png_calculate_lsb_capacity_bytes(&metadata, capacity_bytes);
}

/* Converteste constanta color_type din libpng in string lizibil. */
const char *png_color_type_to_string(const int color_type) {
    switch (color_type) {
        case PNG_COLOR_TYPE_GRAY:
            return "GRAY";
        case PNG_COLOR_TYPE_GRAY_ALPHA:
            return "GRAY_ALPHA";
        case PNG_COLOR_TYPE_PALETTE:
            return "PALETTE";
        case PNG_COLOR_TYPE_RGB:
            return "RGB";
        case PNG_COLOR_TYPE_RGB_ALPHA:
            return "RGBA";
        default:
            return "UNKNOWN";
    }
}
