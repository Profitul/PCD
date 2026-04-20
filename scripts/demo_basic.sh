#!/usr/bin/env bash
# Demo 1: roundtrip text + fisier + capacity/validate cu clientul C.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")"/.. && pwd)"
cd "$ROOT"

COVER="${COVER:-poza/IMG_0003.png}"
MSG="${MSG:-Salutare din demo PCD - steganografie LSB!}"
OUT_DIR="storage/demo"
mkdir -p "$OUT_DIR"

echo "========== [1] Server pornire =========="
./server > logs/server_demo_basic.log 2>&1 &
SPID=$!
sleep 2
if ! kill -0 $SPID 2>/dev/null; then
    echo "Server nu a pornit"; cat logs/server_demo_basic.log; exit 1
fi
trap "kill $SPID 2>/dev/null || true; wait 2>/dev/null || true" EXIT

echo "========== [2] PING =========="
./client ping

echo "========== [3] Capacity =========="
./client capacity "$COVER" | tail -5

echo "========== [4] Validate =========="
./client validate "$COVER" | tail -3

echo "========== [5] Encode text + decode =========="
./client encode-text "$COVER" "$MSG" "$OUT_DIR/enc_text.png" | tail -5
./client decode "$OUT_DIR/enc_text.png" "$OUT_DIR/dec_text.txt" | tail -3
echo "--- mesaj recuperat ---"
cat "$OUT_DIR/dec_text.txt"; echo
if [ "$(cat "$OUT_DIR/dec_text.txt")" = "$MSG" ]; then
    echo "[OK] text roundtrip"
else
    echo "[FAIL] text roundtrip"; exit 1
fi

echo "========== [6] Encode file + decode =========="
PAYLOAD="$OUT_DIR/payload.bin"
python3 -c "import os; open('$PAYLOAD','wb').write(os.urandom(40960))"
./client encode-file "$COVER" "$PAYLOAD" "$OUT_DIR/enc_file.png" | tail -5
./client decode "$OUT_DIR/enc_file.png" "$OUT_DIR/dec_file.bin" | tail -3
if cmp -s "$PAYLOAD" "$OUT_DIR/dec_file.bin"; then
    echo "[OK] file roundtrip 40KB"
else
    echo "[FAIL] file roundtrip"; exit 1
fi

echo "========== DEMO BASIC DONE =========="
