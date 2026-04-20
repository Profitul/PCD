#!/usr/bin/env bash
# Demo 3: fluxul admin (STATS, LISTCLIENTS, BLOCKIP, UNBLOCKIP) in timp ce clienti ruleaza.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")"/.. && pwd)"
cd "$ROOT"

COVER="${COVER:-poza/IMG_0003.png}"
OUT_DIR="storage/demo_admin"
mkdir -p "$OUT_DIR"

echo "========== [1] Server pornire =========="
./server > logs/server_demo_admin.log 2>&1 &
SPID=$!
sleep 2
trap "kill $SPID 2>/dev/null || true; wait 2>/dev/null || true" EXIT

echo "========== [2] Lansare 2 clienti in background =========="
./client encode-text "$COVER" "admin-demo A" "$OUT_DIR/a.png" > "$OUT_DIR/a.log" 2>&1 &
CL1=$!
./client encode-text "$COVER" "admin-demo B" "$OUT_DIR/b.png" > "$OUT_DIR/b.log" 2>&1 &
CL2=$!

sleep 1

echo "========== [3] Sesiune admin (simulata - STATS, LISTCLIENTS, LISTJOBS, BLOCKIP, UNBLOCKIP, HISTORY) =========="
printf '1\n4\n2\n8\n127.0.0.1\n9\n127.0.0.1\n3\n5\n0\n' | ./admin_client | grep -E 'STATS|CLIENTS|JOBS|OK blocked|OK unblocked|HISTORY|AVGDURATION|BYE' | tail -30

wait $CL1 $CL2 2>/dev/null || true
echo "========== DEMO ADMIN DONE =========="
