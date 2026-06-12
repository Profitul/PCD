#!/usr/bin/env bash
# Test extins CANCEL: porneste server cu 1 worker, submit multe joburi TEXT, anuleaza ultimele cat sunt in coada.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")"/.. && pwd)"
source "$ROOT/scripts/demo_lib.sh"

OUT_DIR="storage/test_cancel"
SERVER_LOG="logs/test_cancel_server.log"
mkdir -p "$OUT_DIR"
install_demo_trap

start_server "$SERVER_LOG" --workers 1

section "Submit 10 joburi TEXT prin socket raw"
IDS_FILE="$OUT_DIR/job_ids.txt"
python3 - "$USER_PORT" "$IDS_FILE" <<'PY'
import re, socket, sys
port = int(sys.argv[1])
out = sys.argv[2]
ids = []
for i in range(1, 11):
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.recv(256)
    s.sendall(f"SUBMIT cancel_demo_{i}\n".encode())
    resp = s.recv(256).decode(errors="replace")
    print(resp.strip())
    m = re.search(r"JOB\s+(\d+)", resp)
    if m:
        ids.append(m.group(1))
    s.close()
open(out, "w").write("\n".join(ids) + "\n")
print("ids=", ids)
PY
cat "$IDS_FILE"

mapfile -t IDS < "$IDS_FILE"
if (( ${#IDS[@]} < 10 )); then
    echo "[FAIL] Nu s-au creat 10 joburi" >&2
    exit 1
fi
CANCEL_A="${IDS[7]}"
CANCEL_B="${IDS[8]}"
CANCEL_C="${IDS[9]}"

section "CANCEL pe joburile $CANCEL_A $CANCEL_B $CANCEL_C"
printf '6\n%s\n6\n%s\n6\n%s\n0\n' "$CANCEL_A" "$CANCEL_B" "$CANCEL_C" | admin_menu | grep -E 'OK cancel|ERR|BYE' | tail -20

section "Astept finalizarea si verific HISTORY"
sleep 6
HIST="$OUT_DIR/history.txt"
printf '3\n0\n' | admin_menu > "$HIST" 2>&1
cat "$HIST" | grep -E 'HISTORY|CANCELED|DONE|FAILED|BYE' | tail -40
if grep -q 'CANCELED' "$HIST" || grep -q 'state=CANCELED' "$SERVER_LOG"; then
    echo "[OK] exista joburi CANCELED"
else
    echo "[FAIL] Nu apare CANCELED in history/log" >&2
    exit 1
fi

section "Log cancel relevant"
grep -E 'Cancel by admin|state=CANCELED|Event job_id' "$SERVER_LOG" | tail -30 || true
section "TEST CANCEL EXTINS DONE"
