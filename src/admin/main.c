#include "common.h"
#include "config.h"
#include "net.h"

static int connect_to_admin(const char *host, const uint16_t port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    struct sockaddr_in addr;
    (void)memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        (void)close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        (void)close(fd);
        return -1;
    }

    return fd;
}

static int send_command_and_print(const int fd, const char *cmd) {
    char buffer[BUFFER_SIZE];
    if (write_all(fd, cmd, strlen(cmd)) < 0) {
        return -1;
    }
    if (read_line(fd, buffer, sizeof(buffer)) <= 0) {
        return -1;
    }
    (void)printf("%s", buffer);
    return 0;
}

int main(void) {
    const int fd = connect_to_admin(SERVER_HOST, ADMIN_PORT);
    if (fd < 0) {
        perror("connect_to_admin");
        return EXIT_FAILURE;
    }

    char buffer[BUFFER_SIZE];
    if (read_line(fd, buffer, sizeof(buffer)) > 0) {
        (void)printf("%s", buffer);
    }

    if (send_command_and_print(fd, "STATS\n") < 0) {
        (void)close(fd);
        return EXIT_FAILURE;
    }
    if (send_command_and_print(fd, "LISTJOBS\n") < 0) {
        (void)close(fd);
        return EXIT_FAILURE;
    }
    if (send_command_and_print(fd, "QUIT\n") < 0) {
        (void)close(fd);
        return EXIT_FAILURE;
    }

    (void)close(fd);
    return EXIT_SUCCESS;
}
