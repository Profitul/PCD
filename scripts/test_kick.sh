#!/usr/bin/env bash
# KICK end-to-end: conectez 3 clienti leneși (fac SUBMIT + poll STATUS),
# admin listeaza clientii, extrag un fd, trimite KICK, verifica deconectarea.
set -u
ROOT="$(cd "$(dirname "$0")"/.. && pwd)"
cd "$ROOT"

: > logs/server.log
./server > /dev/null 2>&1 &
SPID=$!
sleep 2
trap "kill $SPID 2>/dev/null || true; wait 2>/dev/null || true" EXIT

echo '[1] Lansez 3 clienti lenesi'
python3 - <<'PY' &
import socket, time, sys
s = socket.create_connection(('127.0.0.1', 9090))
print("client banner:", s.recv(64).decode().strip(), flush=True)
s.sendall(b"SUBMIT lazy_client\n")
print("client:", s.recv(64).decode().strip(), flush=True)
try:
    for _ in range(30):
        s.sendall(b"PING\n")
        data = s.recv(64)
        if not data:
            print("client: connection closed by peer", flush=True)
            break
        time.sleep(0.5)
except Exception as e:
    print("client exc:", e, flush=True)
s.close()
PY
CL1=$!

sleep 1

echo '[2] LISTCLIENTS (extrag fd-urile user)'
printf '4\n0\n' | ./admin_client 2>&1 | grep -oE 'fd=[0-9]+,type=user' | head -3
FD=$(printf '4\n0\n' | ./admin_client 2>&1 | grep -oE 'fd=[0-9]+,type=user' | head -1 | grep -oE '[0-9]+')
echo "    fd_tinta=$FD"

echo '[3] KICK fd='"$FD"
printf "7\n$FD\n0\n" | ./admin_client 2>&1 | grep -E 'OK kicked|ERR' | head -3

sleep 2

echo '[4] Log: Admin kicked'
grep -E 'Admin kicked|Client disconnected' logs/server.log | tail -10

echo '[5] LISTCLIENTS dupa kick (ar trebui sa ramana mai putini)'
printf '4\n0\n' | ./admin_client 2>&1 | grep -oE 'fd=[0-9]+,type=[a-z]+'

wait $CL1 2>/dev/null || true
echo '=== TEST KICK DONE ==='
