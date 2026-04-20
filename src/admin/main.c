#include "common.h"
#include "config.h"
#include "net.h"

#include <ctype.h>

static int connect_to_admin(const char *host, const uint16_t port) {
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

static int send_cmd(const int fd, const char *cmd) {
    return write_all(fd, cmd, strlen(cmd)) < 0 ? -1 : 0;
}

static int recv_and_print(const int fd) {
    char buffer[BUFFER_SIZE * 2];
    const ssize_t n = read_line(fd, buffer, sizeof(buffer));
    if (n <= 0) return -1;
    (void)fputs(buffer, stdout);
    return 0;
}

static void print_menu(void) {
    (void)puts("");
    (void)puts("===============================================");
    (void)puts(" STEGAPNG ADMIN CONSOLE");
    (void)puts("===============================================");
    (void)puts("  1)  STATS         - statistici generale server");
    (void)puts("  2)  LISTJOBS      - lista job-uri active");
    (void)puts("  3)  HISTORY       - istoric job-uri finalizate");
    (void)puts("  4)  LISTCLIENTS   - clienti conectati");
    (void)puts("  5)  AVGDURATION   - durata medie job-uri");
    (void)puts("  6)  CANCEL        - anuleaza job <id>");
    (void)puts("  7)  KICK          - deconecteaza client <fd>");
    (void)puts("  8)  BLOCKIP       - blocheaza un IP");
    (void)puts("  9)  UNBLOCKIP     - deblocheaza un IP");
    (void)puts(" 10)  PING          - test conexiune");
    (void)puts(" 11)  HELP          - comenzile suportate de server");
    (void)puts(" 12)  RAW           - trimite o comanda libera");
    (void)puts("  0)  QUIT          - iesire");
    (void)puts("-----------------------------------------------");
}

static void read_line_stdin(char *buf, size_t buf_size) {
    if (fgets(buf, (int)buf_size, stdin) == NULL) {
        buf[0] = '\0';
        return;
    }
    const size_t n = strlen(buf);
    if (n > 0U && buf[n - 1U] == '\n') buf[n - 1U] = '\0';
}

static int prompt_nonempty(const char *label, char *out, size_t out_size) {
    (void)printf("%s: ", label);
    (void)fflush(stdout);
    read_line_stdin(out, out_size);
    return (out[0] != '\0') ? 0 : -1;
}

static int do_simple(const int fd, const char *cmd) {
    if (send_cmd(fd, cmd) < 0) return -1;
    return recv_and_print(fd);
}

static int do_with_arg(const int fd, const char *verb, const char *label) {
    char arg[256];
    if (prompt_nonempty(label, arg, sizeof(arg)) < 0) {
        (void)puts("(anulat - argument gol)");
        return 0;
    }
    char cmd[512];
    (void)snprintf(cmd, sizeof(cmd), "%s %s\n", verb, arg);
    if (send_cmd(fd, cmd) < 0) return -1;
    return recv_and_print(fd);
}

static int do_raw(const int fd) {
    char line[BUFFER_SIZE];
    if (prompt_nonempty("Comanda (ex. STATS)", line, sizeof(line)) < 0) {
        (void)puts("(anulat)");
        return 0;
    }
    char cmd[BUFFER_SIZE + 2];
    (void)snprintf(cmd, sizeof(cmd), "%s\n", line);
    if (send_cmd(fd, cmd) < 0) return -1;
    return recv_and_print(fd);
}

int main(int argc, char *argv[]) {
    const char *host = (argc > 1) ? argv[1] : SERVER_HOST;
    const uint16_t port = (argc > 2) ? (uint16_t)atoi(argv[2]) : (uint16_t)ADMIN_PORT;

    const int fd = connect_to_admin(host, port);
    if (fd < 0) {
        (void)fprintf(stderr, "Cannot connect to admin port %s:%u\n", host, (unsigned)port);
        return EXIT_FAILURE;
    }

    (void)printf("Connected to admin %s:%u\n", host, (unsigned)port);
    (void)recv_and_print(fd);

    char input[64];
    int running = 1;
    while (running) {
        print_menu();
        (void)printf("Alege: ");
        (void)fflush(stdout);
        read_line_stdin(input, sizeof(input));

        if (input[0] == '\0') continue;
        const int opt = atoi(input);

        int rc = 0;
        switch (opt) {
            case 1:  rc = do_simple(fd, "STATS\n"); break;
            case 2:  rc = do_simple(fd, "LISTJOBS\n"); break;
            case 3:  rc = do_simple(fd, "HISTORY\n"); break;
            case 4:  rc = do_simple(fd, "LISTCLIENTS\n"); break;
            case 5:  rc = do_simple(fd, "AVGDURATION\n"); break;
            case 6:  rc = do_with_arg(fd, "CANCEL", "job id"); break;
            case 7:  rc = do_with_arg(fd, "KICK", "client fd"); break;
            case 8:  rc = do_with_arg(fd, "BLOCKIP", "ip"); break;
            case 9:  rc = do_with_arg(fd, "UNBLOCKIP", "ip"); break;
            case 10: rc = do_simple(fd, "PING\n"); break;
            case 11: rc = do_simple(fd, "HELP\n"); break;
            case 12: rc = do_raw(fd); break;
            case 0:
                (void)do_simple(fd, "QUIT\n");
                running = 0;
                break;
            default:
                (void)puts("Optiune invalida.");
                continue;
        }
        if (rc < 0) {
            (void)fputs("Conexiune intrerupta.\n", stderr);
            running = 0;
        }
    }

    (void)close(fd);
    return EXIT_SUCCESS;
}
