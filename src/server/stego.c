#include "stego.h"

#include <png.h>

typedef struct png_image_buf {
    png_uint_32 width;
    png_uint_32 height;
    int bit_depth;
    int color_type;
    int channels;
    png_bytep *rows;
} png_image_buf_t;

static uint32_t g_crc32_table[256];
static int g_crc32_table_ready = 0;
static pthread_mutex_t g_crc32_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Construieste lookup table-ul CRC32 folosind polinomul IEEE 802.3 (reflected). */
static void crc32_build_table(void) {
    for (uint32_t i = 0U; i < 256U; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            /* Daca bitul cel mai putin semnificativ e setat, XOR cu polinomul;
               altfel shift dreapta (procesare bit cu bit, forma reflectata). */
            c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
        }
        g_crc32_table[i] = c;
    }
}

/* Calculeaza CRC32 pentru un buffer de date.
   Initializeaza lazy (o singura data, protejat de mutex) lookup table-ul. */
static uint32_t stego_crc32(const uint8_t *data, size_t len) {
    (void)pthread_mutex_lock(&g_crc32_mutex);
    if (!g_crc32_table_ready) {
        crc32_build_table();
        g_crc32_table_ready = 1;
    }
    (void)pthread_mutex_unlock(&g_crc32_mutex);

    uint32_t c = 0xFFFFFFFFU;
    for (size_t i = 0U; i < len; i++) {
        /* Combina byte-ul curent cu restul CRC si cauta in table */
        c = g_crc32_table[(c ^ data[i]) & 0xFFU] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFU; /* inversare finala conform standardului CRC32 */
}

/* Scrie un uint32 in format big-endian la adresa dst. */
static void write_be32(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)(v >> 24);
    dst[1] = (uint8_t)(v >> 16);
    dst[2] = (uint8_t)(v >> 8);
    dst[3] = (uint8_t)v;
}

/* Citeste un uint32 din format big-endian de la adresa src. */
static uint32_t read_be32(const uint8_t *src) {
    return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8)  | (uint32_t)src[3];
}

/* Scrie un uint16 in format big-endian la adresa dst. */
static void write_be16(uint8_t *dst, uint16_t v) {
    dst[0] = (uint8_t)(v >> 8);
    dst[1] = (uint8_t)v;
}

/* Citeste un uint16 din format big-endian de la adresa src. */
static uint16_t read_be16(const uint8_t *src) {
    return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

/* Returneaza un string descriptiv pentru un cod de eroare stego. */
const char *stego_strerror(stego_status_t status) {
    switch (status) {
        case STEGO_OK:              return "ok";
        case STEGO_ERR_OPEN:        return "cannot open file";
        case STEGO_ERR_NOT_PNG:     return "not a valid PNG";
        case STEGO_ERR_UNSUPPORTED: return "unsupported PNG format";
        case STEGO_ERR_MEMORY:      return "memory allocation failed";
        case STEGO_ERR_CAPACITY:    return "payload exceeds image capacity";
        case STEGO_ERR_LIBPNG:      return "libpng internal error";
        case STEGO_ERR_MAGIC:       return "no steganography payload found";
        case STEGO_ERR_CRC:         return "payload integrity check failed";
        case STEGO_ERR_IO:          return "I/O error";
        case STEGO_ERR_ARG:         return "invalid argument";
        default:                    return "unknown error";
    }
}

/* Elibereaza memoria alocata pentru randurile imaginii PNG. */
static void png_image_buf_free(png_image_buf_t *img) {
    if (img == NULL) {
        return;
    }
    if (img->rows != NULL) {
        for (png_uint_32 y = 0U; y < img->height; y++) {
            free(img->rows[y]);
        }
        free(img->rows);
        img->rows = NULL;
    }
}

/* Incarca o imagine PNG de la path si o normalizeaza la RGB sau RGBA 8bpp.
   Toate transformarile (palette, grayscale, 16bpp strip, tRNS) sunt aplicate
   astfel incat img->channels devine intotdeauna 3 sau 4. */
static int png_load_rgb_or_rgba(const char *path, png_image_buf_t *img) {
    if (path == NULL || img == NULL) {
        return STEGO_ERR_ARG;
    }

    (void)memset(img, 0, sizeof(*img));

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return STEGO_ERR_OPEN;
    }

    png_byte sig[8];
    if (fread(sig, 1U, sizeof(sig), fp) != sizeof(sig)) {
        (void)fclose(fp);
        return STEGO_ERR_NOT_PNG;
    }
    if (png_sig_cmp(sig, 0, (int)sizeof(sig)) != 0) {
        (void)fclose(fp);
        return STEGO_ERR_NOT_PNG;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) {
        (void)fclose(fp);
        return STEGO_ERR_MEMORY;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        (void)fclose(fp);
        return STEGO_ERR_MEMORY;
    }

    /* setjmp/longjmp este mecanismul de error handling al libpng */
    if (setjmp(png_jmpbuf(png_ptr)) != 0) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        png_image_buf_free(img);
        (void)fclose(fp);
        return STEGO_ERR_LIBPNG;
    }

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, (int)sizeof(sig)); /* anunta libpng ca am citit deja header-ul */
    png_read_info(png_ptr, info_ptr);

    png_uint_32 width  = png_get_image_width(png_ptr, info_ptr);
    png_uint_32 height = png_get_image_height(png_ptr, info_ptr);
    int bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    int color_type = png_get_color_type(png_ptr, info_ptr);

    /* Normalizare format: convertim orice tip de PNG la RGB/RGBA 8bpp */
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS) != 0) {
        png_set_tRNS_to_alpha(png_ptr); /* transparenta pseudo -> canal alpha real */
    }
    if (bit_depth == 16) {
        png_set_strip_16(png_ptr); /* reducem la 8 bpp pentru simplitate */
    }
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_ptr);
    }

    png_read_update_info(png_ptr, info_ptr);

    color_type = png_get_color_type(png_ptr, info_ptr);
    bit_depth  = png_get_bit_depth(png_ptr, info_ptr);
    int channels = png_get_channels(png_ptr, info_ptr);

    if (bit_depth != 8 || (channels != 3 && channels != 4)) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        (void)fclose(fp);
        return STEGO_ERR_UNSUPPORTED;
    }

    png_bytep *rows = (png_bytep *)calloc(height, sizeof(png_bytep));
    if (rows == NULL) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        (void)fclose(fp);
        return STEGO_ERR_MEMORY;
    }

    const size_t rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    for (png_uint_32 y = 0U; y < height; y++) {
        rows[y] = (png_bytep)malloc(rowbytes);
        if (rows[y] == NULL) {
            for (png_uint_32 yy = 0U; yy < y; yy++) {
                free(rows[yy]);
            }
            free(rows);
            png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
            (void)fclose(fp);
            return STEGO_ERR_MEMORY;
        }
    }

    png_read_image(png_ptr, rows);
    png_read_end(png_ptr, NULL);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    (void)fclose(fp);

    img->width = width;
    img->height = height;
    img->bit_depth = bit_depth;
    img->color_type = color_type;
    img->channels = channels;
    img->rows = rows;
    return STEGO_OK;
}

/* Salveaza imaginea PNG (RGB sau RGBA) in fisier. In caz de eroare sterge fisierul partial. */
static int png_save_rgb_or_rgba(const char *path, const png_image_buf_t *img) {
    if (path == NULL || img == NULL || img->rows == NULL) {
        return STEGO_ERR_ARG;
    }

    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return STEGO_ERR_OPEN;
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) {
        (void)fclose(fp);
        (void)unlink(path);
        return STEGO_ERR_MEMORY;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        png_destroy_write_struct(&png_ptr, NULL);
        (void)fclose(fp);
        (void)unlink(path);
        return STEGO_ERR_MEMORY;
    }

    if (setjmp(png_jmpbuf(png_ptr)) != 0) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        (void)fclose(fp);
        (void)unlink(path);
        return STEGO_ERR_LIBPNG;
    }

    png_init_io(png_ptr, fp);

    const int out_color_type = (img->channels == 4) ? PNG_COLOR_TYPE_RGB_ALPHA : PNG_COLOR_TYPE_RGB;

    png_set_IHDR(png_ptr, info_ptr,
                 img->width, img->height,
                 8, out_color_type,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr, info_ptr);
    png_write_image(png_ptr, img->rows);
    png_write_end(png_ptr, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    (void)fclose(fp);
    return STEGO_OK;
}

/* Returneaza numarul de biti disponibili pentru ascunderea datelor.
   Canalul alpha (index 3 in RGBA) este exclus pentru a nu altera transparenta. */
static size_t image_data_bit_capacity(const png_image_buf_t *img) {
    const size_t pixels = (size_t)img->width * (size_t)img->height;
    const int data_channels = (img->channels == 4) ? 3 : img->channels; /* exclude alpha */
    return pixels * (size_t)data_channels;
}

/* Returneaza 1 daca channel-ul cu index ch_index este utilizabil pentru steganografie.
   In RGBA, canalul 3 (alpha) nu este folosit. */
static int is_data_channel(int channels, int ch_index) {
    if (channels == 4 && (ch_index % 4) == 3) {
        return 0;
    }
    return 1;
}

/* Embedeaza datele in LSB-urile pixelilor imaginii (Least Significant Bit steganography).
   Fiecare bit din data este scris in bitul 0 al unui byte de pixel. */
static void embed_bits(png_image_buf_t *img, const uint8_t *data, size_t len_bytes) {
    size_t bit_index = 0U;
    const size_t total_bits = len_bytes * 8U;

    for (png_uint_32 y = 0U; y < img->height && bit_index < total_bits; y++) {
        png_bytep row = img->rows[y];
        const size_t row_len = (size_t)img->width * (size_t)img->channels;
        for (size_t x = 0U; x < row_len && bit_index < total_bits; x++) {
            if (!is_data_channel(img->channels, (int)x)) {
                continue;
            }
            const size_t byte_pos = bit_index / 8U;
            /* Extragem bitul corespunzator din data (LSB first) */
            const uint8_t bit = (uint8_t)((data[byte_pos] >> (bit_index % 8U)) & 1U);
            /* Inlocuim LSB-ul pixelului cu bitul de date, pastrand restul neschimbat */
            row[x] = (png_byte)((row[x] & 0xFEU) | bit);
            bit_index++;
        }
    }
}

/* Extrage datele ascunse din LSB-urile pixelilor imaginii.
   Inversa operatiei embed_bits. */
static void extract_bits(const png_image_buf_t *img, uint8_t *data, size_t len_bytes) {
    (void)memset(data, 0, len_bytes);
    size_t bit_index = 0U;
    const size_t total_bits = len_bytes * 8U;

    for (png_uint_32 y = 0U; y < img->height && bit_index < total_bits; y++) {
        const png_bytep row = img->rows[y];
        const size_t row_len = (size_t)img->width * (size_t)img->channels;
        for (size_t x = 0U; x < row_len && bit_index < total_bits; x++) {
            if (!is_data_channel(img->channels, (int)x)) {
                continue;
            }
            /* Citim LSB-ul pixelului */
            const uint8_t bit = (uint8_t)(row[x] & 1U);
            /* Plasam bitul la pozitia corecta in byte-ul de output */
            data[bit_index / 8U] |= (uint8_t)(bit << (bit_index % 8U));
            bit_index++;
        }
    }
}

/* Construieste buffer-ul complet ce va fi ascuns in imagine.
   Format: [MAGIC 4B][type 1B][fn_len 2B][filename fn_len B][payload_len 4B][payload payload_len B][CRC32 4B]
   Returneaza buffer-ul alocat (trebuie eliberat de apelant) sau NULL la eroare de memorie. */
static uint8_t *build_container(stego_payload_type_t type,
                                const char *filename,
                                const uint8_t *payload,
                                size_t payload_len,
                                size_t *out_len) {
    size_t fn_len = 0U;
    if (type == STEGO_PAYLOAD_FILE && filename != NULL) {
        fn_len = strlen(filename);
        if (fn_len > 255U) {
            fn_len = 255U;
        }
    }

    /* total = MAGIC(4) + type(1) + fn_len_field(2) + filename + payload_len_field(4) + payload + CRC(4) */
    const size_t total = 4U + 1U + 2U + fn_len + 4U + payload_len + 4U;
    uint8_t *buf = (uint8_t *)malloc(total);
    if (buf == NULL) {
        return NULL;
    }

    size_t pos = 0U;
    (void)memcpy(buf + pos, STEGO_MAGIC, STEGO_MAGIC_LEN);
    pos += STEGO_MAGIC_LEN;
    buf[pos++] = (uint8_t)type;
    write_be16(buf + pos, (uint16_t)fn_len);
    pos += 2U;
    if (fn_len > 0U) {
        (void)memcpy(buf + pos, filename, fn_len);
        pos += fn_len;
    }
    write_be32(buf + pos, (uint32_t)payload_len);
    pos += 4U;
    if (payload_len > 0U && payload != NULL) {
        (void)memcpy(buf + pos, payload, payload_len);
        pos += payload_len;
    }

    const uint32_t crc = stego_crc32(buf, pos);
    write_be32(buf + pos, crc);
    pos += 4U;

    *out_len = pos;
    return buf;
}

/* Calculeaza capacitatea maxima de payload (in bytes) pentru o imagine PNG.
   Umple structura out cu dimensiunile imaginii si numarul de bytes utilizabili. */
int stego_get_capacity(const char *png_path, stego_capacity_t *out) {
    if (png_path == NULL || out == NULL) {
        return STEGO_ERR_ARG;
    }

    png_image_buf_t img;
    const int rc = png_load_rgb_or_rgba(png_path, &img);
    if (rc != STEGO_OK) {
        return rc;
    }

    const size_t bits = image_data_bit_capacity(&img);
    const size_t total_bytes = bits / 8U;
    const size_t overhead = STEGO_HEADER_MIN_LEN;

    out->width = img.width;
    out->height = img.height;
    out->channels = img.channels;
    out->data_channels = (img.channels == 4) ? 3 : img.channels;
    out->payload_max_bytes = (total_bytes > overhead) ? (total_bytes - overhead) : 0U;

    png_image_buf_free(&img);
    return STEGO_OK;
}

/* Logica comuna pentru encode text si encode file:
   incarca PNG, construieste containerul, verifica capacitatea si embedeaza. */
static int encode_common(const char *input_png_path,
                         const char *output_png_path,
                         stego_payload_type_t type,
                         const char *filename,
                         const uint8_t *payload,
                         size_t payload_len) {
    if (input_png_path == NULL || output_png_path == NULL) {
        return STEGO_ERR_ARG;
    }

    png_image_buf_t img;
    int rc = png_load_rgb_or_rgba(input_png_path, &img);
    if (rc != STEGO_OK) {
        return rc;
    }

    size_t container_len = 0U;
    uint8_t *container = build_container(type, filename, payload, payload_len, &container_len);
    if (container == NULL) {
        png_image_buf_free(&img);
        return STEGO_ERR_MEMORY;
    }

    const size_t needed_bits = container_len * 8U;
    const size_t avail_bits = image_data_bit_capacity(&img);
    if (needed_bits > avail_bits) {
        free(container);
        png_image_buf_free(&img);
        return STEGO_ERR_CAPACITY;
    }

    embed_bits(&img, container, container_len);
    free(container);

    rc = png_save_rgb_or_rgba(output_png_path, &img);
    png_image_buf_free(&img);
    return rc;
}

/* Ascunde un text in imaginea PNG de la input_png_path si salveaza rezultatul la output_png_path. */
int stego_encode_text(const char *input_png_path,
                      const char *output_png_path,
                      const char *text,
                      size_t text_len) {
    if (text == NULL && text_len > 0U) {
        return STEGO_ERR_ARG;
    }
    return encode_common(input_png_path, output_png_path,
                         STEGO_PAYLOAD_TEXT, NULL,
                         (const uint8_t *)text, text_len);
}

/* Citeste fisierul de la payload_file_path si il ascunde in imaginea PNG.
   store_as_filename este numele salvat in container; daca e NULL, se foloseste basename-ul. */
int stego_encode_file(const char *input_png_path,
                      const char *output_png_path,
                      const char *payload_file_path,
                      const char *store_as_filename) {
    if (payload_file_path == NULL) {
        return STEGO_ERR_ARG;
    }

    FILE *fp = fopen(payload_file_path, "rb");
    if (fp == NULL) {
        return STEGO_ERR_OPEN;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        (void)fclose(fp);
        return STEGO_ERR_IO;
    }
    const long size = ftell(fp);
    if (size < 0) {
        (void)fclose(fp);
        return STEGO_ERR_IO;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        (void)fclose(fp);
        return STEGO_ERR_IO;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (buf == NULL && size > 0) {
        (void)fclose(fp);
        return STEGO_ERR_MEMORY;
    }

    if (size > 0 && fread(buf, 1U, (size_t)size, fp) != (size_t)size) {
        free(buf);
        (void)fclose(fp);
        return STEGO_ERR_IO;
    }
    (void)fclose(fp);

    const char *fn = store_as_filename;
    if (fn == NULL) {
        /* Extrage doar numele fisierului din cale (fara directoare) */
        const char *slash = strrchr(payload_file_path, '/');
        fn = (slash != NULL) ? slash + 1 : payload_file_path;
    }

    const int rc = encode_common(input_png_path, output_png_path,
                                 STEGO_PAYLOAD_FILE, fn, buf, (size_t)size);
    free(buf);
    return rc;
}

/* Extrage payload-ul ascuns dintr-o imagine PNG.
   Aloca out->data (eliberat cu stego_extracted_free), valideaza magic, tip si CRC32. */
int stego_decode(const char *png_path, stego_extracted_t *out) {
    if (png_path == NULL || out == NULL) {
        return STEGO_ERR_ARG;
    }

    (void)memset(out, 0, sizeof(*out));

    png_image_buf_t img;
    int rc = png_load_rgb_or_rgba(png_path, &img);
    if (rc != STEGO_OK) {
        return rc;
    }

    const size_t avail_bytes = image_data_bit_capacity(&img) / 8U;
    if (avail_bytes < STEGO_HEADER_MIN_LEN) {
        png_image_buf_free(&img);
        return STEGO_ERR_MAGIC;
    }

    /* Citim primii 7 bytes: MAGIC(4) + type(1) + fn_len(2) */
    uint8_t header[7];
    extract_bits(&img, header, sizeof(header));

    if (memcmp(header, STEGO_MAGIC, STEGO_MAGIC_LEN) != 0) {
        png_image_buf_free(&img);
        return STEGO_ERR_MAGIC;
    }

    const uint8_t type_byte = header[4];
    if (type_byte != STEGO_PAYLOAD_TEXT && type_byte != STEGO_PAYLOAD_FILE) {
        png_image_buf_free(&img);
        return STEGO_ERR_MAGIC;
    }
    const uint16_t fn_len = read_be16(header + 5);

    const size_t total_needed = 7U + (size_t)fn_len + 4U;
    if (total_needed > avail_bytes) {
        png_image_buf_free(&img);
        return STEGO_ERR_MAGIC;
    }

    uint8_t *probe = (uint8_t *)malloc(total_needed);
    if (probe == NULL) {
        png_image_buf_free(&img);
        return STEGO_ERR_MEMORY;
    }
    extract_bits(&img, probe, total_needed);

    const uint32_t payload_len = read_be32(probe + 7U + fn_len);
    const size_t container_len = 7U + (size_t)fn_len + 4U + (size_t)payload_len + 4U;

    if (container_len > avail_bytes) {
        free(probe);
        png_image_buf_free(&img);
        return STEGO_ERR_MAGIC;
    }

    uint8_t *buf = (uint8_t *)realloc(probe, container_len);
    if (buf == NULL) {
        free(probe);
        png_image_buf_free(&img);
        return STEGO_ERR_MEMORY;
    }
    extract_bits(&img, buf, container_len);
    png_image_buf_free(&img);

    /* Verifica integritatea: CRC32 storat in ultimii 4 bytes vs cel calculat */
    const uint32_t stored_crc = read_be32(buf + container_len - 4U);
    const uint32_t calc_crc = stego_crc32(buf, container_len - 4U);
    if (stored_crc != calc_crc) {
        free(buf);
        return STEGO_ERR_CRC;
    }

    out->type = (stego_payload_type_t)type_byte;
    if (fn_len > 0U) {
        const size_t copy = (fn_len < sizeof(out->filename) - 1U) ? fn_len : sizeof(out->filename) - 1U;
        (void)memcpy(out->filename, buf + 7, copy);
        out->filename[copy] = '\0';
    } else {
        out->filename[0] = '\0';
    }

    if (payload_len > 0U) {
        out->data = (uint8_t *)malloc(payload_len);
        if (out->data == NULL) {
            free(buf);
            return STEGO_ERR_MEMORY;
        }
        (void)memcpy(out->data, buf + 7U + fn_len + 4U, payload_len);
    } else {
        out->data = NULL;
    }
    out->data_len = payload_len;

    free(buf);
    rc = STEGO_OK;
    return rc;
}

/* Elibereaza memoria alocata de stego_decode pentru datele extrase. */
void stego_extracted_free(stego_extracted_t *e) {
    if (e == NULL) {
        return;
    }
    free(e->data);
    e->data = NULL;
    e->data_len = 0U;
}
