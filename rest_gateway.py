#!/usr/bin/env python3
"""
REST gateway minimal pentru serverul StegaPNG existent.

Nu inlocuieste serverul C. Porneste un server HTTP local si traduce cererile
REST/JSON catre protocolul TCP deja implementat de serverul StegaPNG.

Rulare:
    python3 rest_gateway.py --listen 127.0.0.1 --http-port 8080 --stega-host 127.0.0.1 --stega-port 9090

Exemple:
    POST /api/ping
    POST /api/capacity      {"png_b64":"..."}
    POST /api/validate      {"png_b64":"..."}
    POST /api/decode        {"png_b64":"..."}
    POST /api/encode-text   {"png_b64":"...", "text":"mesaj"}
    POST /api/encode-file   {"png_b64":"...", "filename":"secret.txt", "file_b64":"..."}
    GET  /api/jobs/12/status
    GET  /api/jobs/12/result
    GET  /api/jobs/12/meta
    GET  /api/jobs/12/download
"""
from __future__ import annotations

import argparse
import base64
import json
import re
import socket
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

CHUNK_SIZE = 65536
SOCKET_TIMEOUT_S = 30.0
MAX_JSON_BYTES = 32 * 1024 * 1024


def recv_line(sock: socket.socket) -> str:
    data = bytearray()
    while True:
        chunk = sock.recv(1)
        if not chunk:
            break
        data.extend(chunk)
        if chunk == b"\n":
            break
    return data.decode("utf-8", errors="replace").rstrip("\n")


def recv_exact(sock: socket.socket, size: int) -> bytes:
    out = bytearray()
    while len(out) < size:
        chunk = sock.recv(min(CHUNK_SIZE, size - len(out)))
        if not chunk:
            raise ConnectionError("server closed connection while data was expected")
        out.extend(chunk)
    return bytes(out)


def send_line(sock: socket.socket, line: str) -> None:
    if not line.endswith("\n"):
        line += "\n"
    sock.sendall(line.encode("utf-8"))


def parse_job_id(line: str) -> int:
    parts = line.strip().split()
    if len(parts) >= 2 and parts[0] == "JOB":
        return int(parts[1])
    raise RuntimeError(line)


def parse_data_size(line: str) -> int:
    parts = line.strip().split()
    if len(parts) >= 2 and parts[0] == "DATA":
        return int(parts[1])
    raise RuntimeError(line)


class StegaTcpClient:
    def __init__(self, host: str, port: int) -> None:
        self.host = host
        self.port = port
        self.sock: socket.socket | None = None
        self.banner = ""

    def __enter__(self) -> "StegaTcpClient":
        self.sock = socket.create_connection((self.host, self.port), timeout=SOCKET_TIMEOUT_S)
        self.banner = recv_line(self.sock)
        return self

    def __exit__(self, _exc_type: object, _exc: object, _tb: object) -> None:
        if self.sock is not None:
            try:
                send_line(self.sock, "QUIT")
                _ = recv_line(self.sock)
            except OSError:
                pass
            self.sock.close()
            self.sock = None

    def command(self, line: str) -> str:
        assert self.sock is not None
        send_line(self.sock, line)
        return recv_line(self.sock)

    def submit_capacity(self, png: bytes) -> int:
        assert self.sock is not None
        send_line(self.sock, f"CAPACITY {len(png)}")
        self.sock.sendall(png)
        return parse_job_id(recv_line(self.sock))

    def submit_validate(self, png: bytes) -> int:
        assert self.sock is not None
        send_line(self.sock, f"VALIDATE {len(png)}")
        self.sock.sendall(png)
        return parse_job_id(recv_line(self.sock))

    def submit_decode(self, png: bytes) -> int:
        assert self.sock is not None
        send_line(self.sock, f"DECODE {len(png)}")
        self.sock.sendall(png)
        return parse_job_id(recv_line(self.sock))

    def submit_encode_text(self, png: bytes, text: str) -> int:
        assert self.sock is not None
        payload = text.encode("utf-8")
        send_line(self.sock, f"ENCODE_TEXT {len(png)} {len(payload)}")
        self.sock.sendall(png)
        self.sock.sendall(payload)
        return parse_job_id(recv_line(self.sock))

    def submit_encode_file(self, png: bytes, filename: str, payload: bytes) -> int:
        assert self.sock is not None
        name = filename.encode("utf-8")
        send_line(self.sock, f"ENCODE_FILE {len(png)} {len(name)} {len(payload)}")
        self.sock.sendall(png)
        self.sock.sendall(name)
        self.sock.sendall(payload)
        return parse_job_id(recv_line(self.sock))

    def download(self, job_id: int) -> bytes:
        assert self.sock is not None
        send_line(self.sock, f"DOWNLOAD {job_id}")
        header = recv_line(self.sock)
        size = parse_data_size(header)
        return recv_exact(self.sock, size)


class RestHandler(BaseHTTPRequestHandler):
    stega_host = "127.0.0.1"
    stega_port = 9090

    server_version = "StegaREST/1.0"

    def log_message(self, fmt: str, *args: Any) -> None:
        # Pastram logul standard, dar formatat scurt.
        print("%s - %s" % (self.address_string(), fmt % args))

    def send_json(self, status: int, payload: dict[str, Any]) -> None:
        body = json.dumps(payload, ensure_ascii=False, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def read_json(self) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0:
            return {}
        if length > MAX_JSON_BYTES:
            raise ValueError("JSON body too large")
        raw = self.rfile.read(length)
        data = json.loads(raw.decode("utf-8"))
        if not isinstance(data, dict):
            raise ValueError("JSON body must be an object")
        return data

    @staticmethod
    def require_b64(data: dict[str, Any], key: str) -> bytes:
        value = data.get(key)
        if not isinstance(value, str):
            raise ValueError(f"missing string field: {key}")
        return base64.b64decode(value.encode("ascii"), validate=True)

    @staticmethod
    def require_str(data: dict[str, Any], key: str) -> str:
        value = data.get(key)
        if not isinstance(value, str) or value == "":
            raise ValueError(f"missing non-empty string field: {key}")
        return value

    def do_POST(self) -> None:  # noqa: N802 - nume impus de BaseHTTPRequestHandler
        try:
            if self.path == "/api/ping":
                with StegaTcpClient(self.stega_host, self.stega_port) as client:
                    resp = client.command("PING")
                self.send_json(200, {"ok": resp == "PONG", "response": resp})
                return

            data = self.read_json()
            with StegaTcpClient(self.stega_host, self.stega_port) as client:
                if self.path == "/api/capacity":
                    job_id = client.submit_capacity(self.require_b64(data, "png_b64"))
                elif self.path == "/api/validate":
                    job_id = client.submit_validate(self.require_b64(data, "png_b64"))
                elif self.path == "/api/decode":
                    job_id = client.submit_decode(self.require_b64(data, "png_b64"))
                elif self.path == "/api/encode-text":
                    job_id = client.submit_encode_text(
                        self.require_b64(data, "png_b64"),
                        self.require_str(data, "text"),
                    )
                elif self.path == "/api/encode-file":
                    job_id = client.submit_encode_file(
                        self.require_b64(data, "png_b64"),
                        self.require_str(data, "filename"),
                        self.require_b64(data, "file_b64"),
                    )
                else:
                    self.send_json(404, {"ok": False, "error": "unknown endpoint"})
                    return
            self.send_json(202, {"ok": True, "job_id": job_id})
        except Exception as exc:  # raspuns REST prietenos, fara traceback catre client
            self.send_json(400, {"ok": False, "error": str(exc)})

    def do_GET(self) -> None:  # noqa: N802 - nume impus de BaseHTTPRequestHandler
        try:
            match = re.fullmatch(r"/api/jobs/(\d+)/(status|result|meta|download)", self.path)
            if match is None:
                self.send_json(404, {"ok": False, "error": "unknown endpoint"})
                return

            job_id = int(match.group(1))
            action = match.group(2)
            with StegaTcpClient(self.stega_host, self.stega_port) as client:
                if action == "status":
                    resp = client.command(f"STATUS {job_id}")
                    self.send_json(200, {"ok": True, "response": resp})
                elif action == "result":
                    resp = client.command(f"RESULT {job_id}")
                    self.send_json(200, {"ok": True, "response": resp})
                elif action == "meta":
                    resp = client.command(f"META {job_id}")
                    self.send_json(200, {"ok": True, "response": resp})
                elif action == "download":
                    blob = client.download(job_id)
                    self.send_json(200, {
                        "ok": True,
                        "job_id": job_id,
                        "size": len(blob),
                        "data_b64": base64.b64encode(blob).decode("ascii"),
                    })
        except Exception as exc:
            self.send_json(400, {"ok": False, "error": str(exc)})


def main() -> int:
    parser = argparse.ArgumentParser(description="REST gateway pentru StegaPNG TCP server")
    parser.add_argument("--listen", default="127.0.0.1")
    parser.add_argument("--http-port", type=int, default=8080)
    parser.add_argument("--stega-host", default="127.0.0.1")
    parser.add_argument("--stega-port", type=int, default=9090)
    args = parser.parse_args()

    RestHandler.stega_host = args.stega_host
    RestHandler.stega_port = args.stega_port
    server = ThreadingHTTPServer((args.listen, args.http_port), RestHandler)
    print(f"REST gateway listening on http://{args.listen}:{args.http_port}")
    print(f"Proxy target: {args.stega_host}:{args.stega_port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping REST gateway")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
