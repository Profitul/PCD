#include "config.h"
#include "protocol.h"

/* Elimina caracterul '\n' de la sfarsitul string-ului daca exista. */
static void trim_newline(char *text) {
    if (text == NULL) {
        return;
    }

    const size_t len = strlen(text);
    if (len > 0U && text[len - 1U] == '\n') {
        text[len - 1U] = '\0';
    }
}

/* Verifica daca s incepe cu prefix. Returneaza lungimea prefix-ului sau 0 daca nu se potriveste. */
static int starts_with(const char *s, const char *prefix) {
    const size_t n = strlen(prefix);
    return strncmp(s, prefix, n) == 0 ? (int)n : 0;
}

/* Parseaza un uint64 din stringul s. Seteaza *end la primul caracter neconsumit.
   Returneaza 0 la succes, -1 daca parsing-ul esueaza sau valoarea e invalida. */
static int parse_u64(const char *s, uint64_t *out, const char **end) {
    if (s == NULL || out == NULL) {
        return -1;
    }
    char *tail = NULL;
    errno = 0;
    const unsigned long long v = strtoull(s, &tail, 10);
    if (errno != 0 || tail == s) {
        return -1;
    }
    *out = (uint64_t)v;
    if (end != NULL) {
        *end = tail;
    }
    return 0;
}

/* Avanseaza pointerul *p peste spatii si taburi. */
static void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t') {
        (*p)++;
    }
}

/* Parseaza o linie de comanda text in structura protocol_request_t.
   Comenzile cu argumente numerice (ENCODE_TEXT, ENCODE_FILE etc.) folosesc parse_u64
   pentru a citi dimensiunile corespunzatoare. */
int protocol_parse_line(const char *line, protocol_request_t *request) {
    if (line == NULL || request == NULL) {
        return -1;
    }

    (void)memset(request, 0, sizeof(*request));
    request->command = PROTO_CMD_UNKNOWN;

    char copy[BUFFER_SIZE];
    (void)snprintf(copy, sizeof(copy), "%s", line);
    trim_newline(copy);

    if (strcmp(copy, "PING") == 0)       { request->command = PROTO_CMD_PING;      return 0; }
    if (strcmp(copy, "QUIT") == 0)       { request->command = PROTO_CMD_QUIT;      return 0; }
    if (strcmp(copy, "LISTJOBS") == 0)   { request->command = PROTO_CMD_LISTJOBS;  return 0; }
    if (strcmp(copy, "STATS") == 0)      { request->command = PROTO_CMD_STATS;     return 0; }
    if (strcmp(copy, "LISTCLIENTS") == 0){ request->command = PROTO_CMD_LISTCLIENTS; return 0; }
    if (strcmp(copy, "HISTORY") == 0)    { request->command = PROTO_CMD_HISTORY;   return 0; }
    if (strcmp(copy, "AVGDURATION") == 0){ request->command = PROTO_CMD_AVGDURATION; return 0; }
    if (strcmp(copy, "HELP") == 0)       { request->command = PROTO_CMD_HELP;      return 0; }

    int n = 0;
    if ((n = starts_with(copy, "SUBMIT ")) > 0) {
        request->command = PROTO_CMD_SUBMIT;
        (void)snprintf(request->argument1, sizeof(request->argument1), "%s", copy + n);
        return 0;
    }

    if ((n = starts_with(copy, "STATUS ")) > 0) {
        request->command = PROTO_CMD_STATUS;
        request->job_id = strtoull(copy + n, NULL, 10);
        return 0;
    }

    if ((n = starts_with(copy, "RESULT ")) > 0) {
        request->command = PROTO_CMD_RESULT;
        request->job_id = strtoull(copy + n, NULL, 10);
        return 0;
    }

    if ((n = starts_with(copy, "CANCEL ")) > 0) {
        request->command = PROTO_CMD_CANCEL;
        request->job_id = strtoull(copy + n, NULL, 10);
        return 0;
    }

    if ((n = starts_with(copy, "VALIDATE_IMAGE ")) > 0) {
        request->command = PROTO_CMD_VALIDATE_IMAGE;
        (void)snprintf(request->argument1, sizeof(request->argument1), "%s", copy + n);
        return 0;
    }

    if ((n = starts_with(copy, "ANALYZE_CAPACITY ")) > 0) {
        request->command = PROTO_CMD_ANALYZE_CAPACITY;
        (void)snprintf(request->argument1, sizeof(request->argument1), "%s", copy + n);
        return 0;
    }

    if ((n = starts_with(copy, "ENCODE_TEXT ")) > 0) {
        request->command = PROTO_CMD_ENCODE_TEXT;
        const char *p = copy + n;
        const char *e = NULL;
        if (parse_u64(p, &request->size1, &e) < 0) { request->command = PROTO_CMD_UNKNOWN; return 0; }
        p = e; skip_ws(&p);
        if (parse_u64(p, &request->size2, &e) < 0) { request->command = PROTO_CMD_UNKNOWN; return 0; }
        return 0;
    }

    if ((n = starts_with(copy, "ENCODE_FILE ")) > 0) {
        request->command = PROTO_CMD_ENCODE_FILE;
        const char *p = copy + n;
        const char *e = NULL;
        if (parse_u64(p, &request->size1, &e) < 0) { request->command = PROTO_CMD_UNKNOWN; return 0; }
        p = e; skip_ws(&p);
        if (parse_u64(p, &request->size2, &e) < 0) { request->command = PROTO_CMD_UNKNOWN; return 0; }
        p = e; skip_ws(&p);
        if (parse_u64(p, &request->size3, &e) < 0) { request->command = PROTO_CMD_UNKNOWN; return 0; }
        return 0;
    }

    if ((n = starts_with(copy, "DECODE ")) > 0) {
        request->command = PROTO_CMD_DECODE;
        request->size1 = strtoull(copy + n, NULL, 10);
        return 0;
    }

    if ((n = starts_with(copy, "VALIDATE ")) > 0) {
        request->command = PROTO_CMD_VALIDATE;
        request->size1 = strtoull(copy + n, NULL, 10);
        return 0;
    }

    if ((n = starts_with(copy, "CAPACITY ")) > 0) {
        request->command = PROTO_CMD_CAPACITY;
        request->size1 = strtoull(copy + n, NULL, 10);
        return 0;
    }

    if ((n = starts_with(copy, "META ")) > 0) {
        request->command = PROTO_CMD_META;
        request->job_id = strtoull(copy + n, NULL, 10);
        return 0;
    }

    if ((n = starts_with(copy, "DOWNLOAD ")) > 0) {
        request->command = PROTO_CMD_DOWNLOAD;
        request->job_id = strtoull(copy + n, NULL, 10);
        return 0;
    }

    if ((n = starts_with(copy, "KICK ")) > 0) {
        request->command = PROTO_CMD_KICK;
        (void)snprintf(request->argument1, sizeof(request->argument1), "%s", copy + n);
        return 0;
    }

    if ((n = starts_with(copy, "BLOCKIP ")) > 0) {
        request->command = PROTO_CMD_BLOCKIP;
        (void)snprintf(request->argument1, sizeof(request->argument1), "%s", copy + n);
        return 0;
    }

    if ((n = starts_with(copy, "UNBLOCKIP ")) > 0) {
        request->command = PROTO_CMD_UNBLOCKIP;
        (void)snprintf(request->argument1, sizeof(request->argument1), "%s", copy + n);
        return 0;
    }

    return 0;
}
