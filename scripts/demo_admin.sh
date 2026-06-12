#!/usr/bin/env bash
# Demo 3 extins: admin STATS/LISTCLIENTS/LISTJOBS/HISTORY/KICK/BLOCKIP/UNBLOCKIP.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")"/.. && pwd)"
source "$ROOT/scripts/demo_lib.sh"

OUT_DIR="storage/demo_admin"
SERVER_LOG="logs/server_demo_admin.log"
mkdir -p "$OUT_DIR"
install_demo_trap

start_server "$SERVER_LOG"

section "Pornesc 2 clienti lenesi ca sa apara in LISTCLIENTS"
python3 - "$USER_PORT" > "$OUT_DIR/lazy1.log" 2>&1 <<'PY' &
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
print(s.recv(256).decode(errors="replace").strip(), flush=True)
s.sendall(b"SUBMIT lazy_admin_client_1\n")
print(s.recv(256).decode(errors="replace").strip(), flush=True)
try:
    for _ in range(40):
        s.sendall(b"PING\n")
        data = s.recv(256)
        if not data:
            print("closed", flush=True)
            break
        time.sleep(0.25)
except OSError as exc:
    print(f"exc={exc}", flush=True)
s.close()
PY
L1=$!
python3 - "$USER_PORT" > "$OUT_DIR/lazy2.log" 2>&1 <<'PY' &
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
print(s.recv(256).decode(errors="replace").strip(), flush=True)
s.sendall(b"SUBMIT lazy_admin_client_2\n")
print(s.recv(256).decode(errors="replace").strip(), flush=True)
try:
    for _ in range(40):
        s.sendall(b"PING\n")
        data = s.recv(256)
        if not data:
            print("closed", flush=True)
            break
        time.sleep(0.25)
except OSError as exc:
    print(f"exc={exc}", flush=True)
s.close()
PY
L2=$!
sleep 2

section "Admin: STATS, LISTCLIENTS, LISTJOBS"
admin_output="$OUT_DIR/admin_before.txt"
printf '1\n4\n2\n0\n' | admin_menu > "$admin_output" 2>&1
cat "$admin_output" | grep -E 'STATS|CLIENTS|JOBS|BYE' | tail -20

section "Admin KICK pe primul client user"
FD="$(grep -oE 'fd=[0-9]+,type=user' "$admin_output" | head -1 | grep -oE '[0-9]+' || true)"
if [[ -z "$FD" ]]; then
    echo "[FAIL] Nu am gasit fd de client user in LISTCLIENTS" >&2
    cat "$admin_output" >&2
    exit 1
fi
echo "fd_tinta=$FD"
printf '7\n%s\n4\n0\n' "$FD" | admin_menu | grep -E 'OK kicked|CLIENTS|ERR|BYE' | tail -20
sleep 1

section "Admin BLOCKIP / test conexiune refuzata / UNBLOCKIP"
printf '8\n127.0.0.1\n0\n' | admin_menu | grep -E 'OK blocked|ERR|BYE' | tail -10
if py_client ping > "$OUT_DIR/ping_while_blocked.log" 2>&1; then
    echo "[WARN] Ping a mers desi IP-ul a fost blocat. Verifica daca testul ruleaza din 127.0.0.1 sau alt IP."
else
    echo "[OK] client user refuzat cat timp IP-ul e blocat"
    tail -5 "$OUT_DIR/ping_while_blocked.log" || true
fi
printf '9\n127.0.0.1\n0\n' | admin_menu | grep -E 'OK unblocked|ERR|BYE' | tail -10
py_client ping | tail -5

section "Admin HISTORY + AVGDURATION"
printf '3\n5\n0\n' | admin_menu | grep -E 'HISTORY|AVGDURATION|BYE' | tail -20

section "Log admin relevant"
grep -E 'Admin kicked|blocked ip|unblocked|Client disconnected' "$SERVER_LOG" | tail -30 || true

kill "$L1" "$L2" 2>/dev/null || true
wait "$L1" "$L2" 2>/dev/null || true
section "DEMO ADMIN EXTINS DONE"
