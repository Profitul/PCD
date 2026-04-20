#include "config.h"
#include "net.h"

int set_nonblocking(const int fd) {
    const int flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }

    return 0;
}

int create_listen_socket(const uint16_t port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    const int optval = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        (void)close(fd);
        return -1;
    }

    struct sockaddr_in addr;
    (void)memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        (void)close(fd);
        return -1;
    }

    if (listen(fd, 16) < 0) {
        (void)close(fd);
        return -1;
    }

    return fd;
}

ssize_t write_all(const int fd, const void *buffer, const size_t size) {
    if (buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    const char *ptr = (const char *)buffer;
    size_t total = 0U;

    while (total < size) {
        const ssize_t written = write(fd, ptr + total, size - total);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (written == 0) {
            break;
        }

        total += (size_t)written;
    }

    return (ssize_t)total;
}

ssize_t read_line(const int fd, char *buffer, const size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0U) {
        errno = EINVAL;
        return -1;
    }

    size_t pos = 0U;

    while (pos + 1U < buffer_size) {
        char ch = '\0';
        const ssize_t rc = read(fd, &ch, 1U);

        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (rc == 0) {
            break;
        }

        buffer[pos] = ch;
        pos++;

        if (ch == '\n') {
            break;
        }
    }

    buffer[pos] = '\0';
    return (ssize_t)pos;
}

ssize_t read_exact(const int fd, void *buffer, const size_t size) {
    if (buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    char *ptr = (char *)buffer;
    size_t total = 0U;

    while (total < size) {
        const ssize_t rc = read(fd, ptr + total, size - total);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (rc == 0) {
            return (ssize_t)total;
        }
        total += (size_t)rc;
    }

    return (ssize_t)total;
}

int discard_exact(const int fd, const size_t size) {
    char buf[4096];
    size_t remaining = size;
    while (remaining > 0U) {
        const size_t want = (remaining < sizeof(buf)) ? remaining : sizeof(buf);
        const ssize_t rc = read(fd, buf, want);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (rc == 0) {
            return -1;
        }
        remaining -= (size_t)rc;
    }
    return 0;
}
