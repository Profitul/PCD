#!/usr/bin/env bash
# Demo 2: 4 clienti paraleli (2 C + 2 Python) demonstrand coada FIFO + worker pool.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")"/.. && pwd)"
cd "$ROOT"

COVER="${COVER:-poza/IMG_0003.png}"
OUT_DIR="storage/demo_concurrent"
mkdir -p "$OUT_DIR"

echo "========== [1] Server pornire =========="
./server > logs/server_demo_concurrent.log 2>&1 &
SPID=$!
sleep 2
trap "kill $SPID 2>/dev/null || true; wait 2>/dev/null || true" EXIT

echo "========== [2] Lansare 4 clienti paraleli =========="
./client encode-text "$COVER" "job A din C" "$OUT_DIR/a.png" > "$OUT_DIR/a.log" 2>&1 &
PIDS="$!"
./client encode-text "$COVER" "job B din C" "$OUT_DIR/b.png" > "$OUT_DIR/b.log" 2>&1 &
PIDS="$PIDS $!"
python3 python_client/steg_client.py encode-text "$COVER" "job C din Python" "$OUT_DIR/c.png" > "$OUT_DIR/c.log" 2>&1 &
PIDS="$PIDS $!"
python3 python_client/steg_client.py encode-text "$COVER" "job D din Python" "$OUT_DIR/d.png" > "$OUT_DIR/d.log" 2>&1 &
PIDS="$PIDS $!"

echo "PIDs: $PIDS"
for p in $PIDS; do wait $p; done

echo "========== [3] Verificare 4 rezultate =========="
for name in a b c d; do
    if [ -s "$OUT_DIR/$name.png" ]; then
        echo "[OK] $name.png ($(stat -c%s "$OUT_DIR/$name.png") bytes)"
    else
        echo "[FAIL] $name.png lipseste"; exit 1
    fi
done

echo "========== [4] Decode fiecare si verificare =========="
EXPECTED=("job A din C" "job B din C" "job C din Python" "job D din Python")
i=0
for name in a b c d; do
    ./client decode "$OUT_DIR/$name.png" "$OUT_DIR/$name.txt" > /dev/null 2>&1
    GOT="$(cat "$OUT_DIR/$name.txt")"
    if [ "$GOT" = "${EXPECTED[$i]}" ]; then
        echo "[OK] $name: $GOT"
    else
        echo "[FAIL] $name: $GOT != ${EXPECTED[$i]}"; exit 1
    fi
    i=$((i + 1))
done

echo "========== DEMO CONCURRENT DONE =========="
