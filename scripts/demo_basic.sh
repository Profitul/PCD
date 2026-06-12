#!/usr/bin/env bash
# Demo 1 extins: INET + UNIX socket + validate/capacity + text/file roundtrip + INotify.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")"/.. && pwd)"
source "$ROOT/scripts/demo_lib.sh"

OUT_DIR="storage/demo_basic"
SERVER_LOG="logs/server_demo_basic.log"
MSG="${MSG:-Salutare din demo PCD - INET + UNIX + LSB + INotify!}"
mkdir -p "$OUT_DIR"
install_demo_trap

start_server "$SERVER_LOG"

section "PING pe INET cu client C/Python"
user_client ping
py_client ping

section "PING pe UNIX/LOCAL socket"
py_unix_client ping

section "VALIDATE + CAPACITY pe imaginea cover"
user_client validate "$COVER" | tail -8
user_client capacity "$COVER" | tail -8

section "ENCODE_TEXT + DECODE, verificare mesaj"
ENC_TEXT="$OUT_DIR/encoded_text.png"
DEC_TEXT="$OUT_DIR/decoded_text.txt"
user_client encode-text "$COVER" "$MSG" "$ENC_TEXT" | tail -8
assert_file_nonempty "$ENC_TEXT"
py_unix_client decode "$ENC_TEXT" "$DEC_TEXT" | tail -8
assert_file_nonempty "$DEC_TEXT"
printf 'Mesaj extras: '
cat "$DEC_TEXT"
printf '\n'
if [[ "$(cat "$DEC_TEXT")" != "$MSG" ]]; then
    echo "[FAIL] mesajul decodat difera" >&2
    exit 1
fi
echo "[OK] text roundtrip prin encode pe INET si decode pe UNIX"

section "ENCODE_FILE + DECODE, verificare binara"
PAYLOAD="$OUT_DIR/payload_16kb.bin"
ENC_FILE="$OUT_DIR/encoded_file.png"
DEC_FILE="$OUT_DIR/decoded_file.bin"
python3 - <<PY
from pathlib import Path
Path('$PAYLOAD').write_bytes(bytes((i % 251 for i in range(16 * 1024))))
PY
py_client encode-file "$COVER" "$PAYLOAD" "$ENC_FILE" | tail -8
assert_file_nonempty "$ENC_FILE"
user_client decode "$ENC_FILE" "$DEC_FILE" | tail -8
assert_file_nonempty "$DEC_FILE"
if cmp -s "$PAYLOAD" "$DEC_FILE"; then
    echo "[OK] file roundtrip 16KB"
else
    echo "[FAIL] fisierul decodat difera" >&2
    exit 1
fi

section "Admin STATS + HISTORY dupa joburi"
printf '1\n3\n5\n0\n' | admin_menu | grep -E 'STATS|HISTORY|AVGDURATION|BYE' | tail -20

section "Dovezi INotify din log"
grep -E 'INotify|inotify|storage event|IN_CREATE|IN_CLOSE_WRITE|IN_MODIFY|IN_MOVED' "$SERVER_LOG" | tail -20 || \
    echo "[WARN] Nu am gasit linii INotify in log. Verifica formatul exact din server.c"

section "DEMO BASIC EXTINS DONE"
