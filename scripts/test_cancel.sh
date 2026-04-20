#!/usr/bin/env bash
# Cancel end-to-end: folosesc SUBMIT (job TEXT, ~2.5s cu cancel checks la 100ms).
# 3 workeri, submit 9 joburi; joburile 4..9 stau QUEUED; cancel pe 7/8/9 inca QUEUED.
set -u
ROOT="$(cd "$(dirname "$0")"/.. && pwd)"
cd "$ROOT"

: > logs/server.log
./server > /dev/null 2>&1 &
SPID=$!
sleep 2
trap "kill $SPID 2>/dev/null || true; wait 2>/dev/null || true" EXIT

echo '[1] Submit 9 joburi TEXT via python socket'
python3 - <<'PY'
import socket, threading
def submit(i):
    s = socket.create_connection(('127.0.0.1', 9090))
    s.recv(128)
    s.sendall(f"SUBMIT msg_{i}\n".encode())
    resp = s.recv(128).decode()
    print(f"  client {i}: {resp.strip()}")
    try:
        for _ in range(60):
            s.sendall(b"STATUS 0\n")
            s.recv(128)
    except Exception:
        pass
    s.close()
ts = []
for i in range(1, 10):
    t = threading.Thread(target=submit, args=(i,))
    t.daemon = True
    t.start()
    ts.append(t)
import time
time.sleep(0.5)
print("=== all submitted ===")
PY

echo '[2] CANCEL 7, 8, 9 (inca QUEUED)'
printf '6\n7\n6\n8\n6\n9\n0\n' | ./admin_client 2>&1 | grep -E 'OK cancel|ERR cannot|ERR job' | head -10

sleep 4

echo '[3] Log: Cancel by admin'
grep -E 'Cancel by admin' logs/server.log | tail -5

echo '[4] Log: state=CANCELED'
grep -E 'state=CANCELED' logs/server.log | tail -5

echo '[5] HISTORY'
printf '3\n0\n' | ./admin_client 2>&1 | grep -oE 'id=[0-9]+,state=[A-Z]+' | sort -u

echo '=== TEST CANCEL DONE ==='
