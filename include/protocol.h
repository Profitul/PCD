#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "common.h"

typedef enum client_type {
    CLIENT_TYPE_UNKNOWN = 0,
    CLIENT_TYPE_USER = 1,
    CLIENT_TYPE_ADMIN = 2
} client_type_t;

typedef enum protocol_command {
    PROTO_CMD_UNKNOWN = 0,
    PROTO_CMD_PING = 1,
    PROTO_CMD_QUIT = 2,
    PROTO_CMD_SUBMIT = 3,
    PROTO_CMD_STATUS = 4,
    PROTO_CMD_RESULT = 5,
    PROTO_CMD_CANCEL = 6,
    PROTO_CMD_LISTJOBS = 7,
    PROTO_CMD_STATS = 8,
    PROTO_CMD_VALIDATE_IMAGE = 9,
    PROTO_CMD_ANALYZE_CAPACITY = 10,
    PROTO_CMD_ENCODE_TEXT = 11,
    PROTO_CMD_ENCODE_FILE = 12,
    PROTO_CMD_DECODE = 13,
    PROTO_CMD_VALIDATE = 14,
    PROTO_CMD_CAPACITY = 15,
    PROTO_CMD_META = 16,
    PROTO_CMD_DOWNLOAD = 17,
    PROTO_CMD_LISTCLIENTS = 18,
    PROTO_CMD_HISTORY = 19,
    PROTO_CMD_KICK = 20,
    PROTO_CMD_BLOCKIP = 21,
    PROTO_CMD_UNBLOCKIP = 22,
    PROTO_CMD_AVGDURATION = 23,
    PROTO_CMD_HELP = 24
} protocol_command_t;

typedef struct protocol_request {
    protocol_command_t command;
    uint64_t job_id;
    uint64_t size1;
    uint64_t size2;
    uint64_t size3;
    char argument1[BUFFER_SIZE];
    char argument2[BUFFER_SIZE];
} protocol_request_t;

int protocol_parse_line(const char *line, protocol_request_t *request);

#endif
