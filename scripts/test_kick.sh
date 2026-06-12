#!/usr/bin/env bash
# Test extins KICK: client lenes conectat, admin extrage fd, trimite KICK, verifica inchidere.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")"/.. && pwd)"
source "$ROOT/scripts/demo_lib.sh"

OUT_DIR="storage/test_kick"
SERVER_LOG="logs/test_kick_server.log"
mkdir -p "$OUT_DIR"
install_demo_trap

start_server "$SERVER_LOG"

section "Pornesc client lenes"
python3 - "$USER_PORT" > "$OUT_DIR/lazy.log" 2>&1 <<'PY' &
import socket, sys, time
port = int(sys.argv[1])
deadline = time.time() + 8
last = None
while True:
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=1)
        break
    except OSError as exc:
        last = exc
        if time.time() > deadline:
            raise
        time.sleep(0.2)
print("banner", s.recv(256).decode(errors="replace").strip(), flush=True)
s.sendall(b"SUBMIT kick_demo_client\n")
print("submit", s.recv(256).decode(errors="replace").strip(), flush=True)
try:
    while True:
        s.sendall(b"PING\n")
        data = s.recv(256)
        if not data:
            print("closed_by_server", flush=True)
            break
        print(data.decode(errors="replace").strip(), flush=True)
        time.sleep(0.3)
except OSError as exc:
    print("socket_error", exc, flush=True)
s.close()
PY
CLPID=$!
for _ in $(seq 1 40); do
    if grep -q "submit" "$OUT_DIR/lazy.log" 2>/dev/null; then
        break
    fi
    sleep 0.2
done
if ! grep -q "submit" "$OUT_DIR/lazy.log" 2>/dev/null; then
    echo "[FAIL] clientul lenes nu s-a conectat. Log:" >&2
    cat "$OUT_DIR/lazy.log" >&2 || true
    exit 1
fi

section "LISTCLIENTS si extrag fd"
LIST="$OUT_DIR/listclients.txt"
printf '4\n0\n' | admin_menu > "$LIST" 2>&1
cat "$LIST" | grep -E 'CLIENTS|fd=' | tail -20
FD="$(grep -oE 'fd=[0-9]+,type=user' "$LIST" | head -1 | grep -oE '[0-9]+' || true)"
if [[ -z "$FD" ]]; then
    echo "[FAIL] Nu am gasit client user" >&2
    cat "$LIST" >&2
    exit 1
fi
echo "fd_tinta=$FD"

section "KICK fd=$FD"
printf '7\n%s\n0\n' "$FD" | admin_menu | grep -E 'OK kicked|ERR|BYE' | tail -10
sleep 1

section "Verific client/log"
cat "$OUT_DIR/lazy.log" | tail -20
if grep -Eq 'closed_by_server|socket_error|ERR kicked' "$OUT_DIR/lazy.log" || grep -q 'Admin kicked' "$SERVER_LOG"; then
    echo "[OK] kick observat"
else
    echo "[FAIL] nu se vede efectul KICK" >&2
    exit 1
fi

kill "$CLPID" 2>/dev/null || true
wait "$CLPID" 2>/dev/null || true
section "TEST KICK EXTINS DONE"
