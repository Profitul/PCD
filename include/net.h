#ifndef NET_H
#define NET_H

#include "common.h"

int create_listen_socket(uint16_t port);
int set_nonblocking(int fd);
ssize_t read_line(int fd, char *buffer, size_t buffer_size);
ssize_t write_all(int fd, const void *buffer, size_t size);
ssize_t read_exact(int fd, void *buffer, size_t size);
int discard_exact(int fd, size_t size);

#endif
