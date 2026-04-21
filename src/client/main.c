#include "common.h"
#include "config.h"
#include "net.h"

#include <unistd.h>

/* Creeaza un socket TCP si se conecteaza la serverul de pe host:port.
   Returneaza fd-ul socketului sau -1 la eroare. */
static int connect_to_server(const char *host, const uint16_t port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    (void)memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) { (void)close(fd); return -1; }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { (void)close(fd); return -1; }
    return fd;
}

/* Citeste si afiseaza mesajul de bun-venit trimis de server la conectare. */
static int read_banner(const int fd) {
    char buf[BUFFER_SIZE];
    if (read_line(fd, buf, sizeof(buf)) <= 0) return -1;
    (void)printf("<< %s", buf);
    return 0;
}

static int expect_line(const int fd, char *buf, size_t buf_size) {
    if (read_line(fd, buf, buf_size) <= 0) return -1;
    (void)printf("<< %s", buf);
    return 0;
}

static int send_line(const int fd, const char *line) {
    (void)printf(">> %s", line);
    return write_all(fd, line, strlen(line)) < 0 ? -1 : 0;
}

/* Trimite continutul fisierului de la path pe socket in bucati de CHUNK_SIZE.
   Seteaza *size_out la dimensiunea fisierului. */
static int send_file_bytes(const int fd, const char *path, off_t *size_out) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) { perror("fopen"); return -1; }
    if (fseek(fp, 0, SEEK_END) != 0) { (void)fclose(fp); return -1; }
    const long sz = ftell(fp);
    if (sz < 0) { (void)fclose(fp); return -1; }
    if (fseek(fp, 0, SEEK_SET) != 0) { (void)fclose(fp); return -1; }
    *size_out = (off_t)sz;

    char buf[CHUNK_SIZE];
    while (1) {
        const size_t r = fread(buf, 1U, sizeof(buf), fp);
        if (r > 0U) {
            if (write_all(fd, buf, r) < 0) { (void)fclose(fp); return -1; }
        }
        if (r < sizeof(buf)) {
            if (ferror(fp) != 0) { (void)fclose(fp); return -1; }
            break;
        }
    }
    (void)fclose(fp);
    return 0;
}

/* Polleaza starea jobului la fiecare 250ms pana la terminare sau timeout de 30 secunde. */
static int wait_for_done(const int fd, uint64_t job_id) {
    char buf[BUFFER_SIZE];
    for (int i = 0; i < 120; i++) {
        char cmd[64];
        (void)snprintf(cmd, sizeof(cmd), "STATUS %llu\n", (unsigned long long)job_id);
        if (send_line(fd, cmd) < 0) return -1;
        if (expect_line(fd, buf, sizeof(buf)) < 0) return -1;
        if (strstr(buf, "DONE") != NULL) return 0;
        if (strstr(buf, "FAILED") != NULL) return -1;
        if (strstr(buf, "CANCELED") != NULL) return -1;
        struct timespec ts = {0, 250000000L};
        (void)nanosleep(&ts, NULL);
    }
    return -1;
}

/* Extrage job_id din raspunsul "JOB <id>" al serverului. */
static uint64_t parse_job_id(const char *line) {
    const char *p = strstr(line, "JOB ");
    if (p == NULL) return 0;
    return strtoull(p + 4, NULL, 10);
}

/* Extrage dimensiunea din raspunsul "DATA <size>" al serverului. */
static long parse_data_size(const char *line) {
    if (strncmp(line, "DATA ", 5) != 0) return -1;
    return strtol(line + 5, NULL, 10);
}

/* Trimite DOWNLOAD <job_id>, primeste "DATA <size>" si salveaza bytes-ii in out_path. */
static int download_result(const int fd, uint64_t job_id, const char *out_path) {
    char cmd[64], buf[BUFFER_SIZE];
    (void)snprintf(cmd, sizeof(cmd), "DOWNLOAD %llu\n", (unsigned long long)job_id);
    if (send_line(fd, cmd) < 0) return -1;
    if (expect_line(fd, buf, sizeof(buf)) < 0) return -1;
    const long size = parse_data_size(buf);
    if (size < 0) {
        (void)fprintf(stderr, "download refused: %s", buf);
        return -1;
    }
    FILE *fp = fopen(out_path, "wb");
    if (fp == NULL) { perror("fopen out"); return -1; }
    char data[CHUNK_SIZE];
    long remaining = size;
    while (remaining > 0) {
        const size_t want = (remaining < (long)sizeof(data)) ? (size_t)remaining : sizeof(data);
        const ssize_t r = read_exact(fd, data, want);
        if (r <= 0) { (void)fclose(fp); return -1; }
        if (fwrite(data, 1U, (size_t)r, fp) != (size_t)r) { (void)fclose(fp); return -1; }
        remaining -= r;
    }
    (void)fclose(fp);
    (void)printf("   saved %ld bytes to %s\n", size, out_path);
    return 0;
}

/* Trimite comanda ENCODE_TEXT urmata de PNG si textul de ascuns, apoi descarca rezultatul. */
static int cmd_encode_text(const int fd, const char *png_in, const char *text, const char *out_png) {
    off_t png_size = 0;
    struct stat st;
    if (stat(png_in, &st) < 0) { perror("stat"); return -1; }
    png_size = st.st_size;
    const size_t text_len = strlen(text);

    char line[256];
    (void)snprintf(line, sizeof(line), "ENCODE_TEXT %lld %zu\n",
                   (long long)png_size, text_len);
    if (send_line(fd, line) < 0) return -1;
    off_t dummy;
    if (send_file_bytes(fd, png_in, &dummy) < 0) return -1;
    if (write_all(fd, text, text_len) < 0) return -1;

    char buf[BUFFER_SIZE];
    if (expect_line(fd, buf, sizeof(buf)) < 0) return -1;
    const uint64_t job_id = parse_job_id(buf);
    if (job_id == 0U) return -1;

    if (wait_for_done(fd, job_id) < 0) {
        (void)send_line(fd, "RESULT 0\n");
        char cmd[64]; (void)snprintf(cmd, sizeof(cmd), "RESULT %llu\n", (unsigned long long)job_id);
        (void)send_line(fd, cmd);
        (void)expect_line(fd, buf, sizeof(buf));
        return -1;
    }
    char cmd[64]; (void)snprintf(cmd, sizeof(cmd), "META %llu\n", (unsigned long long)job_id);
    (void)send_line(fd, cmd);
    (void)expect_line(fd, buf, sizeof(buf));
    return download_result(fd, job_id, out_png);
}

/* Trimite comanda ENCODE_FILE: PNG + filename + fisierul payload, descarca PNG-ul rezultat. */
static int cmd_encode_file(const int fd, const char *png_in, const char *file_in, const char *out_png) {
    struct stat s1, s2;
    if (stat(png_in, &s1) < 0) { perror("stat png"); return -1; }
    if (stat(file_in, &s2) < 0) { perror("stat file"); return -1; }
    const char *base = strrchr(file_in, '/');
    base = (base != NULL) ? base + 1 : file_in;
    const size_t name_len = strlen(base);

    char line[256];
    (void)snprintf(line, sizeof(line), "ENCODE_FILE %lld %zu %lld\n",
                   (long long)s1.st_size, name_len, (long long)s2.st_size);
    if (send_line(fd, line) < 0) return -1;
    off_t dummy;
    if (send_file_bytes(fd, png_in, &dummy) < 0) return -1;
    if (write_all(fd, base, name_len) < 0) return -1;
    if (send_file_bytes(fd, file_in, &dummy) < 0) return -1;

    char buf[BUFFER_SIZE];
    if (expect_line(fd, buf, sizeof(buf)) < 0) return -1;
    const uint64_t job_id = parse_job_id(buf);
    if (job_id == 0U) return -1;
    if (wait_for_done(fd, job_id) < 0) return -1;
    return download_result(fd, job_id, out_png);
}

/* Trimite comanda DECODE cu PNG-ul, asteapta terminarea jobului si descarca payload-ul extras. */
static int cmd_decode(const int fd, const char *png_in, const char *out_path) {
    struct stat s;
    if (stat(png_in, &s) < 0) { perror("stat"); return -1; }

    char line[128];
    (void)snprintf(line, sizeof(line), "DECODE %lld\n", (long long)s.st_size);
    if (send_line(fd, line) < 0) return -1;
    off_t dummy;
    if (send_file_bytes(fd, png_in, &dummy) < 0) return -1;

    char buf[BUFFER_SIZE];
    if (expect_line(fd, buf, sizeof(buf)) < 0) return -1;
    const uint64_t job_id = parse_job_id(buf);
    if (job_id == 0U) return -1;
    if (wait_for_done(fd, job_id) < 0) return -1;
    char cmd[64];
    (void)snprintf(cmd, sizeof(cmd), "META %llu\n", (unsigned long long)job_id);
    (void)send_line(fd, cmd);
    (void)expect_line(fd, buf, sizeof(buf));
    return download_result(fd, job_id, out_path);
}

/* Trimite comanda CAPACITY, asteapta jobul si afiseaza capacitatea LSB a imaginii. */
static int cmd_capacity(const int fd, const char *png_in) {
    struct stat s;
    if (stat(png_in, &s) < 0) { perror("stat"); return -1; }
    char line[128];
    (void)snprintf(line, sizeof(line), "CAPACITY %lld\n", (long long)s.st_size);
    if (send_line(fd, line) < 0) return -1;
    off_t dummy;
    if (send_file_bytes(fd, png_in, &dummy) < 0) return -1;
    char buf[BUFFER_SIZE];
    if (expect_line(fd, buf, sizeof(buf)) < 0) return -1;
    const uint64_t job_id = parse_job_id(buf);
    if (job_id == 0U) return -1;
    if (wait_for_done(fd, job_id) < 0) return -1;
    char cmd[64];
    (void)snprintf(cmd, sizeof(cmd), "RESULT %llu\n", (unsigned long long)job_id);
    (void)send_line(fd, cmd);
    (void)expect_line(fd, buf, sizeof(buf));
    return 0;
}

/* Trimite comanda VALIDATE, asteapta jobul si afiseaza metadatele PNG-ului. */
static int cmd_validate(const int fd, const char *png_in) {
    struct stat s;
    if (stat(png_in, &s) < 0) { perror("stat"); return -1; }
    char line[128];
    (void)snprintf(line, sizeof(line), "VALIDATE %lld\n", (long long)s.st_size);
    if (send_line(fd, line) < 0) return -1;
    off_t dummy;
    if (send_file_bytes(fd, png_in, &dummy) < 0) return -1;
    char buf[BUFFER_SIZE];
    if (expect_line(fd, buf, sizeof(buf)) < 0) return -1;
    const uint64_t job_id = parse_job_id(buf);
    if (job_id == 0U) return -1;
    if (wait_for_done(fd, job_id) < 0) return -1;
    char cmd[64];
    (void)snprintf(cmd, sizeof(cmd), "RESULT %llu\n", (unsigned long long)job_id);
    (void)send_line(fd, cmd);
    (void)expect_line(fd, buf, sizeof(buf));
    return 0;
}

static void usage(const char *argv0) {
    (void)fprintf(stderr,
        "usage:\n"
        "  %s encode-text <input.png> <text> <output.png>\n"
        "  %s encode-file <input.png> <payload> <output.png>\n"
        "  %s decode <input.png> <output_path>\n"
        "  %s capacity <input.png>\n"
        "  %s validate <input.png>\n"
        "  %s ping\n",
        argv0, argv0, argv0, argv0, argv0, argv0);
}

/* Entry point: parseaza comanda din argv, se conecteaza la server si executa comanda. */
int main(int argc, char *argv[]) {
    if (argc < 2) { usage(argv[0]); return EXIT_FAILURE; }

    const int fd = connect_to_server(SERVER_HOST, SERVER_PORT);
    if (fd < 0) { perror("connect"); return EXIT_FAILURE; }
    (void)read_banner(fd);

    int rc = 0;
    if (strcmp(argv[1], "ping") == 0) {
        (void)send_line(fd, "PING\n");
        char buf[BUFFER_SIZE];
        rc = expect_line(fd, buf, sizeof(buf));
    } else if (strcmp(argv[1], "encode-text") == 0 && argc == 5) {
        rc = cmd_encode_text(fd, argv[2], argv[3], argv[4]);
    } else if (strcmp(argv[1], "encode-file") == 0 && argc == 5) {
        rc = cmd_encode_file(fd, argv[2], argv[3], argv[4]);
    } else if (strcmp(argv[1], "decode") == 0 && argc == 4) {
        rc = cmd_decode(fd, argv[2], argv[3]);
    } else if (strcmp(argv[1], "capacity") == 0 && argc == 3) {
        rc = cmd_capacity(fd, argv[2]);
    } else if (strcmp(argv[1], "validate") == 0 && argc == 3) {
        rc = cmd_validate(fd, argv[2]);
    } else {
        usage(argv[0]);
        (void)close(fd);
        return EXIT_FAILURE;
    }

    (void)send_line(fd, "QUIT\n");
    char buf[BUFFER_SIZE];
    (void)expect_line(fd, buf, sizeof(buf));
    (void)close(fd);
    return (rc == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}