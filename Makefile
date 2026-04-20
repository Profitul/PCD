CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -Werror -g -std=c11 -D_POSIX_C_SOURCE=200809L -pthread -Iinclude
LDFLAGS = -pthread -lpng

SERVER_SRCS = \
	src/server/main.c \
	src/server/server.c \
	src/server/net.c \
	src/server/logger.c \
	src/server/protocol.c \
	src/server/job.c \
	src/server/queue.c \
	src/server/worker.c \
	src/server/storage.c \
	src/server/png_utils.c \
	src/server/stego.c

CLIENT_SRCS = \
	src/client/main.c \
	src/server/net.c

ADMIN_SRCS = \
	src/admin/main.c \
	src/server/net.c

STEGO_TEST_SRCS = \
	tests/stego_test.c \
	src/server/stego.c

SERVER_BIN = server
CLIENT_BIN = client
ADMIN_BIN = admin_client
STEGO_TEST_BIN = stego_test

ANALYZE_SRCS = $(SERVER_SRCS) src/client/main.c src/admin/main.c tests/stego_test.c
ANALYZE_FLAGS = -fanalyzer -Wall -Wextra -Wpedantic -std=c11 -D_POSIX_C_SOURCE=200809L -pthread -Iinclude

.PHONY: all clean run-server run-client run-admin test analyze

all: $(SERVER_BIN) $(CLIENT_BIN) $(ADMIN_BIN)

$(SERVER_BIN): $(SERVER_SRCS)
	$(CC) $(CFLAGS) $(SERVER_SRCS) -o $(SERVER_BIN) $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_SRCS)
	$(CC) $(CFLAGS) $(CLIENT_SRCS) -o $(CLIENT_BIN) -pthread

$(ADMIN_BIN): $(ADMIN_SRCS)
	$(CC) $(CFLAGS) $(ADMIN_SRCS) -o $(ADMIN_BIN) -pthread

$(STEGO_TEST_BIN): $(STEGO_TEST_SRCS)
	$(CC) $(CFLAGS) $(STEGO_TEST_SRCS) -o $(STEGO_TEST_BIN) $(LDFLAGS)

test: $(STEGO_TEST_BIN)
	./$(STEGO_TEST_BIN)
run-server: $(SERVER_BIN)
	./$(SERVER_BIN)

run-client: $(CLIENT_BIN)
	./$(CLIENT_BIN)

run-admin: $(ADMIN_BIN)
	./$(ADMIN_BIN)

analyze:
	@rc=0; for f in $(ANALYZE_SRCS); do \
		echo "== analyze $$f =="; \
		$(CC) -c $(ANALYZE_FLAGS) $$f -o /tmp/_analyze.o || rc=1; \
	done; rm -f /tmp/_analyze.o; exit $$rc

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN) $(ADMIN_BIN) $(STEGO_TEST_BIN)
