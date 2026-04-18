#include "stego.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check(int rc, const char *label) {
    if (rc != STEGO_OK) {
        (void)fprintf(stderr, "[FAIL] %s: %s (rc=%d)\n", label, stego_strerror(rc), rc);
        return 1;
    }
    (void)printf("[ OK ] %s\n", label);
    return 0;
}

static int write_bytes(const char *path, const uint8_t *data, size_t len) {
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return -1;
    }
    const size_t w = fwrite(data, 1U, len, fp);
    (void)fclose(fp);
    return (w == len) ? 0 : -1;
}

static int read_bytes(const char *path, uint8_t **data, size_t *len) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        (void)fclose(fp);
        return -1;
    }
    const long sz = ftell(fp);
    if (sz < 0) {
        (void)fclose(fp);
        return -1;
    }
    (void)fseek(fp, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (buf == NULL && sz > 0) {
        (void)fclose(fp);
        return -1;
    }
    if (sz > 0 && fread(buf, 1U, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        (void)fclose(fp);
        return -1;
    }
    (void)fclose(fp);
    *data = buf;
    *len = (size_t)sz;
    return 0;
}

int main(int argc, char *argv[]) {
    const char *cover = (argc > 1) ? argv[1] : "poza/IMG_0003.png";
    const char *out_text = "storage/temp/test_text_out.png";
    const char *out_file = "storage/temp/test_file_out.png";
    const char *payload_src = "storage/temp/test_payload.bin";

    (void)system("mkdir -p storage/temp");

    int failures = 0;

    (void)printf("=== capacity ===\n");
    stego_capacity_t cap;
    failures += check(stego_get_capacity(cover, &cap), "capacity");
    (void)printf("       %ux%u channels=%d data_channels=%d max_payload=%zu bytes\n",
                 cap.width, cap.height, cap.channels, cap.data_channels, cap.payload_max_bytes);

    (void)printf("=== text roundtrip ===\n");
    const char *msg = "salutare din pcd, test roundtrip LSB!";
    const size_t msg_len = strlen(msg);
    failures += check(stego_encode_text(cover, out_text, msg, msg_len), "encode_text");

    stego_extracted_t e1;
    failures += check(stego_decode(out_text, &e1), "decode_text");
    if (e1.type != STEGO_PAYLOAD_TEXT) {
        (void)fprintf(stderr, "[FAIL] text roundtrip: type mismatch (got %d)\n", e1.type);
        failures++;
    } else if (e1.data_len != msg_len || memcmp(e1.data, msg, msg_len) != 0) {
        (void)fprintf(stderr, "[FAIL] text roundtrip: content mismatch\n");
        failures++;
    } else {
        (void)printf("[ OK ] text bytes match (%zu)\n", msg_len);
    }
    stego_extracted_free(&e1);

    (void)printf("=== file roundtrip ===\n");
    uint8_t sample[4096];
    for (size_t i = 0U; i < sizeof(sample); i++) {
        sample[i] = (uint8_t)((i * 31U + 7U) & 0xFFU);
    }
    if (write_bytes(payload_src, sample, sizeof(sample)) != 0) {
        (void)fprintf(stderr, "[FAIL] cannot write sample payload\n");
        return 1;
    }

    failures += check(stego_encode_file(cover, out_file, payload_src, "secret.bin"), "encode_file");

    stego_extracted_t e2;
    failures += check(stego_decode(out_file, &e2), "decode_file");
    if (e2.type != STEGO_PAYLOAD_FILE) {
        (void)fprintf(stderr, "[FAIL] file roundtrip: type mismatch (got %d)\n", e2.type);
        failures++;
    } else if (strcmp(e2.filename, "secret.bin") != 0) {
        (void)fprintf(stderr, "[FAIL] file roundtrip: filename mismatch (%s)\n", e2.filename);
        failures++;
    } else if (e2.data_len != sizeof(sample) || memcmp(e2.data, sample, sizeof(sample)) != 0) {
        (void)fprintf(stderr, "[FAIL] file roundtrip: content mismatch (%zu vs %zu)\n",
                      e2.data_len, sizeof(sample));
        failures++;
    } else {
        (void)printf("[ OK ] file bytes match (name=%s size=%zu)\n", e2.filename, e2.data_len);
    }
    stego_extracted_free(&e2);

    (void)printf("=== negative tests ===\n");
    stego_extracted_t e3;
    const int rc = stego_decode(cover, &e3);
    if (rc == STEGO_ERR_MAGIC) {
        (void)printf("[ OK ] decode of unmodified cover correctly returns STEGO_ERR_MAGIC\n");
    } else {
        (void)fprintf(stderr, "[FAIL] expected STEGO_ERR_MAGIC, got %d (%s)\n", rc, stego_strerror(rc));
        failures++;
        stego_extracted_free(&e3);
    }

    if (failures == 0) {
        (void)printf("\n=== ALL TESTS PASSED ===\n");
        return 0;
    }
    (void)fprintf(stderr, "\n=== %d TEST(S) FAILED ===\n", failures);

    uint8_t *read_payload = NULL;
    size_t read_len = 0U;
    if (read_bytes(payload_src, &read_payload, &read_len) == 0) {
        free(read_payload);
    }
    return 1;
}
