#include "config.h"
#include "logger.h"
#include "server.h"

int main(void) {
    if (logger_init(LOG_PATH) < 0) {
        perror("logger_init");
        return EXIT_FAILURE;
    }

    const int rc = server_run();
    logger_close();
    return rc;
}