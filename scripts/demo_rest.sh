#!/usr/bin/env bash
# Demo 4: REST/S-R gateway peste serverul TCP: ping, capacity, validate, encode-text, decode, download.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")"/.. && pwd)"
source "$ROOT/scripts/demo_lib.sh"

OUT_DIR="storage/demo_rest"
SERVER_LOG="logs/server_demo_rest.log"
REST_LOG="logs/rest_gateway_demo.log"
HTTP_PORT="${HTTP_PORT:-18080}"
mkdir -p "$OUT_DIR"
install_demo_trap

start_server "$SERVER_LOG"

if port_is_busy "$HTTP_PORT"; then
    echo "[FAIL] HTTP_PORT=$HTTP_PORT este deja ocupat. Alege altul: HTTP_PORT=18081 $0" >&2
    exit 1
fi

section "Pornesc REST gateway pe http://127.0.0.1:$HTTP_PORT"
: > "$REST_LOG"
python3 rest_gateway.py --listen 127.0.0.1 --http-port "$HTTP_PORT" --stega-host 127.0.0.1 --stega-port "$USER_PORT" > "$REST_LOG" 2>&1 &
REST_PID=$!
python3 - "$HTTP_PORT" "$DEMO_TIMEOUT_S" <<'PYWAIT'
import json, sys, time, urllib.request
port = int(sys.argv[1])
timeout = float(sys.argv[2])
deadline = time.time() + timeout
last = None
while time.time() < deadline:
    try:
        req = urllib.request.Request(f"http://127.0.0.1:{port}/api/ping", data=b"{}", method="POST", headers={"Content-Type":"application/json"})
        with urllib.request.urlopen(req, timeout=1) as r:
            json.loads(r.read().decode("utf-8"))
        sys.exit(0)
    except Exception as exc:
        last = exc
        time.sleep(0.2)
print(f"REST gateway not ready: {last}", file=sys.stderr)
sys.exit(1)
PYWAIT
if ! kill -0 "$REST_PID" 2>/dev/null; then
    echo "[FAIL] REST gateway nu a pornit. Log:" >&2
    cat "$REST_LOG" >&2
    exit 1
fi

section "REST requests: ping, capacity, validate, encode-text, poll status, download, decode"
python3 - "$HTTP_PORT" "$COVER" "$OUT_DIR" <<'PY'
import base64, json, sys, time, urllib.request
from pathlib import Path

port = int(sys.argv[1])
cover_path = Path(sys.argv[2])
out_dir = Path(sys.argv[3])
base = f"http://127.0.0.1:{port}"
png_b64 = base64.b64encode(cover_path.read_bytes()).decode("ascii")

def post(path, payload=None):
    data = json.dumps(payload or {}).encode("utf-8")
    req = urllib.request.Request(base + path, data=data, method="POST", headers={"Content-Type":"application/json"})
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.loads(r.read().decode("utf-8"))

def get(path):
    with urllib.request.urlopen(base + path, timeout=20) as r:
        return json.loads(r.read().decode("utf-8"))

def wait_done(job_id):
    for _ in range(120):
        st = get(f"/api/jobs/{job_id}/status")
        print("status", job_id, st)
        if "DONE" in str(st):
            return st
        if "FAILED" in str(st) or "CANCELED" in str(st):
            raise SystemExit(f"job failed: {st}")
        time.sleep(0.25)
    raise SystemExit("timeout")

print("PING", post("/api/ping"))
cap = post("/api/capacity", {"png_b64": png_b64})
print("CAPACITY submit", cap)
wait_done(cap["job_id"])
print("CAPACITY result", get(f"/api/jobs/{cap['job_id']}/result"))

val = post("/api/validate", {"png_b64": png_b64})
print("VALIDATE submit", val)
wait_done(val["job_id"])
print("VALIDATE result", get(f"/api/jobs/{val['job_id']}/result"))

msg = "mesaj prin REST gateway"
enc = post("/api/encode-text", {"png_b64": png_b64, "text": msg})
print("ENCODE_TEXT submit", enc)
wait_done(enc["job_id"])
dl = get(f"/api/jobs/{enc['job_id']}/download")
enc_png = base64.b64decode(dl["data_b64"])
(out_dir / "rest_encoded.png").write_bytes(enc_png)
print("downloaded encoded png", len(enc_png))

dec = post("/api/decode", {"png_b64": base64.b64encode(enc_png).decode("ascii")})
print("DECODE submit", dec)
wait_done(dec["job_id"])
dl2 = get(f"/api/jobs/{dec['job_id']}/download")
text = base64.b64decode(dl2["data_b64"]).decode("utf-8")
(out_dir / "rest_decoded.txt").write_text(text, encoding="utf-8")
print("decoded text", text)
if text != msg:
    raise SystemExit("decoded REST text mismatch")
print("[OK] REST roundtrip")
PY

section "REST gateway log"
tail -20 "$REST_LOG" || true
section "DEMO REST DONE"
