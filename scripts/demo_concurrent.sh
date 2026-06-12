#!/usr/bin/env bash
# Demo 2 extins: clienti paraleli C + Python + UNIX, coada, worker pool, verificare rezultate.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")"/.. && pwd)"
source "$ROOT/scripts/demo_lib.sh"

OUT_DIR="storage/demo_concurrent"
SERVER_LOG="logs/server_demo_concurrent.log"
mkdir -p "$OUT_DIR"
install_demo_trap

start_server "$SERVER_LOG"

section "Lansez 6 joburi paralele: C, Python INET si Python UNIX"
declare -a PIDS=()
run_job() {
    local mode="$1" name="$2" msg="$3"
    local out_png="$OUT_DIR/$name.png"
    local log="$OUT_DIR/$name.log"
    case "$mode" in
        c)    user_client encode-text "$COVER" "$msg" "$out_png" > "$log" 2>&1 ;;
        py)   py_client encode-text "$COVER" "$msg" "$out_png" > "$log" 2>&1 ;;
        unix) py_unix_client encode-text "$COVER" "$msg" "$out_png" > "$log" 2>&1 ;;
        *) echo "bad mode" >&2; exit 2 ;;
    esac
}

run_job c    a "job A din client C" & PIDS+=("$!")
run_job c    b "job B din client C" & PIDS+=("$!")
run_job py   c "job C din Python INET" & PIDS+=("$!")
run_job py   d "job D din Python INET" & PIDS+=("$!")
run_job unix e "job E din Python UNIX" & PIDS+=("$!")
run_job unix f "job F din Python UNIX" & PIDS+=("$!")

echo "PIDs: ${PIDS[*]}"
for pid in "${PIDS[@]}"; do
    wait "$pid"
done

section "Verific fisiere PNG produse"
for name in a b c d e f; do
    assert_file_nonempty "$OUT_DIR/$name.png"
done

section "Decode + verificare continut pentru toate joburile"
declare -A EXPECTED=(
    [a]="job A din client C"
    [b]="job B din client C"
    [c]="job C din Python INET"
    [d]="job D din Python INET"
    [e]="job E din Python UNIX"
    [f]="job F din Python UNIX"
)
for name in a b c d e f; do
    py_client decode "$OUT_DIR/$name.png" "$OUT_DIR/$name.txt" > "$OUT_DIR/$name.decode.log" 2>&1
    got="$(cat "$OUT_DIR/$name.txt")"
    if [[ "$got" != "${EXPECTED[$name]}" ]]; then
        echo "[FAIL] $name: '$got' != '${EXPECTED[$name]}'" >&2
        exit 1
    fi
    echo "[OK] $name: $got"
done

section "Admin STATS + LISTJOBS + HISTORY"
printf '1\n2\n3\n0\n' | admin_menu | grep -E 'STATS|JOBS|HISTORY|BYE' | tail -30

section "Log worker/coada"
grep -E 'Worker|Submitted job|Event job_id' "$SERVER_LOG" | tail -30 || true

section "DEMO CONCURRENT EXTINS DONE"
