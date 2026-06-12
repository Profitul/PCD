#!/usr/bin/env bash
# Common helpers pentru demo-urile StegaPNG.
# Se include cu: source scripts/demo_lib.sh

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
cd "$ROOT"

USER_PORT="${USER_PORT:-9090}"
ADMIN_PORT="${ADMIN_PORT:-9091}"
UNIX_SOCKET="${UNIX_SOCKET:-/tmp/stegapng_user.sock}"
COVER="${COVER:-poza/IMG_0003.png}"
DEMO_TIMEOUT_S="${DEMO_TIMEOUT_S:-10}"
SERVER_PID=""
REST_PID=""

section() {
    printf "
========== %s ==========
" "$1"
}

need_file() {
    if [[ ! -f "$1" ]]; then
        echo "[FAIL] Lipseste fisierul: $1" >&2
        exit 1
    fi
}

need_exe() {
    if [[ ! -x "$1" ]]; then
        echo "[FAIL] Lipseste executabilul: $1" >&2
        echo "Ruleaza: make prepare-runtime && make all" >&2
        exit 1
    fi
}

prepare_demo_runtime() {
    mkdir -p logs storage/uploads storage/results storage/temp
    rm -f "$UNIX_SOCKET"
    need_file "$COVER"
    need_exe ./server
    need_exe ./client
    need_exe ./admin_client
}

port_is_busy() {
    local port="$1"
    ss -ltn 2>/dev/null | awk '{print $4}' | grep -Eq "(^|:)${port}$"
}

assert_ports_free() {
    local busy=0
    for port in "$USER_PORT" "$ADMIN_PORT"; do
        if port_is_busy "$port"; then
            echo "[FAIL] Portul $port este deja ocupat." >&2
            busy=1
        fi
    done
    if [[ "$busy" -ne 0 ]]; then
        echo "Opreste serverul vechi, de exemplu:" >&2
        echo "  pkill -f './server' || true" >&2
        echo "  rm -f /tmp/stegapng_user.sock" >&2
        echo "sau ruleaza demo-ul cu alte porturi:" >&2
        echo "  USER_PORT=10090 ADMIN_PORT=10091 UNIX_SOCKET=/tmp/stegapng_demo.sock $0" >&2
        exit 1
    fi
}

wait_tcp() {
    local port="$1"
    python3 - "$port" "$DEMO_TIMEOUT_S" <<'PY'
import socket, sys, time
port = int(sys.argv[1])
timeout = float(sys.argv[2])
deadline = time.time() + timeout
last = None
while time.time() < deadline:
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=0.25)
        try:
            s.recv(128)
        except OSError:
            pass
        s.close()
        sys.exit(0)
    except OSError as exc:
        last = exc
        time.sleep(0.1)
print(f"timeout waiting for TCP port {port}: {last}", file=sys.stderr)
sys.exit(1)
PY
}

wait_unix_socket() {
    local path="$1"
    local deadline=$((SECONDS + DEMO_TIMEOUT_S))
    while (( SECONDS < deadline )); do
        [[ -S "$path" ]] && return 0
        sleep 0.1
    done
    echo "[FAIL] UNIX socket nu a aparut: $path" >&2
    return 1
}

start_server() {
    local log_path="$1"
    shift || true
    prepare_demo_runtime
    assert_ports_free
    : > "$log_path"
    section "Pornesc serverul: user=$USER_PORT admin=$ADMIN_PORT unix=$UNIX_SOCKET"
    ./server --port "$USER_PORT" --admin-port "$ADMIN_PORT" --unix-socket "$UNIX_SOCKET" --log "$log_path" "$@" > /tmp/stegapng_demo_server_stderr.log 2>&1 &
    SERVER_PID=$!
    sleep 0.3
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "[FAIL] Serverul s-a oprit imediat. Log:" >&2
        cat "$log_path" >&2
        exit 1
    fi
    wait_tcp "$USER_PORT"
    wait_tcp "$ADMIN_PORT"
    wait_unix_socket "$UNIX_SOCKET"
    echo "[OK] server pid=$SERVER_PID"
}

stop_background_services() {
    if [[ -n "${REST_PID:-}" ]]; then
        kill "$REST_PID" 2>/dev/null || true
        wait "$REST_PID" 2>/dev/null || true
        REST_PID=""
    fi
    if [[ -n "${SERVER_PID:-}" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
    fi
    rm -f "$UNIX_SOCKET"
}

install_demo_trap() {
    trap stop_background_services EXIT INT TERM
}

user_client() {
    if [[ "$USER_PORT" == "9090" ]]; then
        ./client "$@"
    else
        python3 python_client/steg_client.py --host 127.0.0.1 --port "$USER_PORT" "$@"
    fi
}

py_client() {
    python3 python_client/steg_client.py --host 127.0.0.1 --port "$USER_PORT" "$@"
}

py_unix_client() {
    python3 python_client/steg_client.py --unix-socket "$UNIX_SOCKET" "$@"
}

admin_menu() {
    ./admin_client 127.0.0.1 "$ADMIN_PORT"
}

admin_raw() {
    local command="$1"
    python3 - "$ADMIN_PORT" "$command" <<'PY'
import socket, sys
port = int(sys.argv[1])
cmd = sys.argv[2]
s = socket.create_connection(("127.0.0.1", port), timeout=5)
print(s.recv(4096).decode(errors="replace"), end="")
s.sendall((cmd.rstrip("\n") + "\n").encode())
print(s.recv(65536).decode(errors="replace"), end="")
s.sendall(b"QUIT\n")
try:
    print(s.recv(4096).decode(errors="replace"), end="")
except OSError:
    pass
s.close()
PY
}

assert_file_nonempty() {
    if [[ ! -s "$1" ]]; then
        echo "[FAIL] Fisier lipsa sau gol: $1" >&2
        exit 1
    fi
    echo "[OK] $1 ($(stat -c%s "$1") bytes)"
}
