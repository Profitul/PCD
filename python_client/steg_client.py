#!/usr/bin/env python3
"""
Client Python pentru serverul stegapng.
Suporta aceleasi comenzi ca si clientul C: encode-text, encode-file,
decode, capacity, validate, ping.
"""
import argparse
import os
import socket
import sys
import time
from typing import Optional

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 9090
DEFAULT_UNIX_SOCKET = "/tmp/stegapng_user.sock"
CHUNK_SIZE = 65536
STATUS_POLL_INTERVAL_S = 0.25
STATUS_POLL_MAX_ITER = 120
USER_COMMANDS = {"ping", "capacity", "validate", "encode-text", "encode-file", "decode"}


def normalize_argv_for_optional_unix_socket(argv: list[str]) -> list[str]:
    """Permite si forma scurta: --unix-socket ping.

    argparse cu nargs=? ar interpreta 'ping' ca path, nu ca subcomanda.
    Rescriem manual doar cazul in care urmatorul token este o subcomanda.
    """
    out: list[str] = []
    i = 0
    while i < len(argv):
        current = argv[i]
        if current == "--unix-socket" and (i + 1 == len(argv) or argv[i + 1] in USER_COMMANDS):
            out.append("--unix-socket=" + DEFAULT_UNIX_SOCKET)
            i += 1
            continue
        out.append(current)
        i += 1
    return out


def connect_tcp(host: str, port: int) -> socket.socket:
    """Creeaza o conexiune TCP/INET la host:port cu timeout de 30 secunde."""
    sock = socket.create_connection((host, port), timeout=30.0)
    return sock


def connect_unix(path: str) -> socket.socket:
    """Creeaza o conexiune UNIX/LOCAL/FILE la socket-ul indicat de path."""
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(30.0)
    try:
        sock.connect(path)
    except OSError:
        sock.close()
        raise
    return sock


def connect(host: str, port: int, unix_socket: Optional[str] = None) -> socket.socket:
    """Alege transportul: UNIX socket daca unix_socket este setat, altfel TCP/INET."""
    if unix_socket is not None:
        return connect_unix(unix_socket)
    return connect_tcp(host, port)


def recv_line(sock: socket.socket) -> str:
    """Citeste o linie terminata cu '\\n' din socket, byte cu byte.
    Decodifica UTF-8 cu inlocuire in caz de caractere invalide."""
    data = bytearray()
    while True:
        ch = sock.recv(1)
        if not ch:
            break
        data.extend(ch)
        if ch == b"\n":
            break
    return data.decode("utf-8", errors="replace")


def recv_exact(sock: socket.socket, n: int) -> bytes:
    """Citeste exact n bytes din socket, looping pana la completare.
    Arunca ConnectionError daca peer-ul inchide conexiunea prematur."""
    buf = bytearray()
    while len(buf) < n:
        # citim cel mult CHUNK_SIZE bytes sau cat mai e de citit
        chunk = sock.recv(min(CHUNK_SIZE, n - len(buf)))
        if not chunk:
            raise ConnectionError("peer closed while expecting %d bytes" % n)
        buf.extend(chunk)
    return bytes(buf)


def send_line(sock: socket.socket, line: str) -> None:
    """Trimite o linie de comanda pe socket, adaugand '\\n' daca lipseste.
    Afiseaza comanda trimisa cu prefix '>>'."""
    if not line.endswith("\n"):
        line = line + "\n"
    print(">> " + line, end="")
    sock.sendall(line.encode("utf-8"))


def expect_line(sock: socket.socket) -> str:
    """Citeste o linie de raspuns de la server si o afiseaza cu prefix '<<'."""
    line = recv_line(sock)
    print("<< " + line, end="")
    return line


def send_file(sock: socket.socket, path: str) -> int:
    """Trimite continutul fisierului de la path pe socket in bucati de CHUNK_SIZE.
    Returneaza dimensiunea fisierului in bytes."""
    size = os.path.getsize(path)
    with open(path, "rb") as fp:
        while True:
            chunk = fp.read(CHUNK_SIZE)
            if not chunk:
                break
            sock.sendall(chunk)
    return size


def parse_job_id(line: str) -> int:
    """Extrage job_id din raspunsul 'JOB <id>'. Returneaza 0 daca formatul e invalid."""
    parts = line.strip().split()
    if len(parts) < 2 or parts[0] != "JOB":
        return 0
    try:
        return int(parts[1])
    except ValueError:
        return 0


def parse_data_size(line: str) -> int:
    """Extrage dimensiunea din raspunsul 'DATA <size>'. Returneaza -1 daca e invalid."""
    parts = line.strip().split()
    if len(parts) < 2 or parts[0] != "DATA":
        return -1
    try:
        return int(parts[1])
    except ValueError:
        return -1


def wait_for_done(sock: socket.socket, job_id: int) -> bool:
    """Polleaza starea jobului la fiecare STATUS_POLL_INTERVAL_S secunde.
    Returneaza True daca jobul a terminat cu DONE, False la FAILED/CANCELED/timeout."""
    for _ in range(STATUS_POLL_MAX_ITER):
        send_line(sock, "STATUS %d" % job_id)
        resp = expect_line(sock)
        if "DONE" in resp:
            return True
        if "FAILED" in resp or "CANCELED" in resp:
            return False
        time.sleep(STATUS_POLL_INTERVAL_S)
    return False  # timeout dupa STATUS_POLL_MAX_ITER iteratii


def download_result(sock: socket.socket, job_id: int, out_path: str) -> bool:
    """Trimite DOWNLOAD <job_id>, primeste 'DATA <size>' si salveaza datele in out_path.
    Returneaza True la succes, False daca serverul refuza download-ul."""
    send_line(sock, "DOWNLOAD %d" % job_id)
    resp = expect_line(sock)
    size = parse_data_size(resp)
    if size < 0:
        print("download refused: " + resp, file=sys.stderr)
        return False
    with open(out_path, "wb") as fp:
        remaining = size
        while remaining > 0:
            want = min(CHUNK_SIZE, remaining)
            chunk = recv_exact(sock, want)
            fp.write(chunk)
            remaining -= len(chunk)
    print("   saved %d bytes to %s" % (size, out_path))
    return True


def cmd_ping(sock: socket.socket) -> int:
    """Trimite PING si asteapta PONG. Returneaza 0 la succes."""
    send_line(sock, "PING")
    expect_line(sock)
    return 0


def cmd_capacity(sock: socket.socket, png_in: str) -> int:
    """Trimite PNG-ul la server si afiseaza capacitatea LSB disponibila in bytes."""
    size = os.path.getsize(png_in)
    send_line(sock, "CAPACITY %d" % size)
    send_file(sock, png_in)
    resp = expect_line(sock)
    job_id = parse_job_id(resp)
    if job_id == 0:
        return 1
    if not wait_for_done(sock, job_id):
        return 1
    send_line(sock, "RESULT %d" % job_id)
    expect_line(sock)
    return 0


def cmd_validate(sock: socket.socket, png_in: str) -> int:
    """Trimite PNG-ul la server si afiseaza metadatele (dimensiuni, bit depth, color type)."""
    size = os.path.getsize(png_in)
    send_line(sock, "VALIDATE %d" % size)
    send_file(sock, png_in)
    resp = expect_line(sock)
    job_id = parse_job_id(resp)
    if job_id == 0:
        return 1
    if not wait_for_done(sock, job_id):
        return 1
    send_line(sock, "RESULT %d" % job_id)
    expect_line(sock)
    return 0


def cmd_encode_text(sock: socket.socket, png_in: str, text: str, out_png: str) -> int:
    """Ascunde textul in PNG si salveaza PNG-ul rezultat la out_png.
    Protocolul: ENCODE_TEXT <png_size> <text_size> -> PNG bytes -> text bytes -> JOB id."""
    png_size = os.path.getsize(png_in)
    text_bytes = text.encode("utf-8")
    send_line(sock, "ENCODE_TEXT %d %d" % (png_size, len(text_bytes)))
    send_file(sock, png_in)
    sock.sendall(text_bytes)  # textul e trimis direct, fara fisier intermediar
    resp = expect_line(sock)
    job_id = parse_job_id(resp)
    if job_id == 0:
        return 1
    if not wait_for_done(sock, job_id):
        return 1
    send_line(sock, "META %d" % job_id)
    expect_line(sock)
    return 0 if download_result(sock, job_id, out_png) else 1


def cmd_encode_file(sock: socket.socket, png_in: str, file_in: str, out_png: str) -> int:
    """Ascunde fisierul in PNG si salveaza rezultatul la out_png.
    Protocolul: ENCODE_FILE <png_sz> <name_sz> <file_sz> -> PNG -> name -> file."""
    png_size = os.path.getsize(png_in)
    file_size = os.path.getsize(file_in)
    name = os.path.basename(file_in)  # doar numele fisierului, fara cale
    name_bytes = name.encode("utf-8")
    send_line(sock, "ENCODE_FILE %d %d %d" % (png_size, len(name_bytes), file_size))
    send_file(sock, png_in)
    sock.sendall(name_bytes)
    send_file(sock, file_in)
    resp = expect_line(sock)
    job_id = parse_job_id(resp)
    if job_id == 0:
        return 1
    if not wait_for_done(sock, job_id):
        return 1
    return 0 if download_result(sock, job_id, out_png) else 1


def cmd_decode(sock: socket.socket, png_in: str, out_path: str) -> int:
    """Extrage payload-ul ascuns din PNG si il salveaza la out_path."""
    size = os.path.getsize(png_in)
    send_line(sock, "DECODE %d" % size)
    send_file(sock, png_in)
    resp = expect_line(sock)
    job_id = parse_job_id(resp)
    if job_id == 0:
        return 1
    if not wait_for_done(sock, job_id):
        return 1
    send_line(sock, "META %d" % job_id)
    expect_line(sock)
    return 0 if download_result(sock, job_id, out_path) else 1


def main() -> int:
    """Entry point: parseaza argumentele, se conecteaza la server si executa comanda aleasa."""
    parser = argparse.ArgumentParser(prog="steg_client.py")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument(
        "--unix-socket",
        default=None,
        metavar="PATH",
        help=(
            "foloseste socket UNIX/LOCAL/FILE. Pentru path-ul implicit poti scrie "
            "--unix-socket ping sau --unix-socket=%s ping" % DEFAULT_UNIX_SOCKET
        ),
    )
    parser.add_argument(
        "--unix",
        action="store_true",
        help="scurtatura pentru --unix-socket=%s" % DEFAULT_UNIX_SOCKET,
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("ping")

    p = sub.add_parser("capacity"); p.add_argument("png_in")
    p = sub.add_parser("validate"); p.add_argument("png_in")

    p = sub.add_parser("encode-text")
    p.add_argument("png_in"); p.add_argument("text"); p.add_argument("out_png")

    p = sub.add_parser("encode-file")
    p.add_argument("png_in"); p.add_argument("file_in"); p.add_argument("out_png")

    p = sub.add_parser("decode")
    p.add_argument("png_in"); p.add_argument("out_path")

    args = parser.parse_args(normalize_argv_for_optional_unix_socket(sys.argv[1:]))

    unix_socket = args.unix_socket
    if args.unix and unix_socket is None:
        unix_socket = DEFAULT_UNIX_SOCKET

    sock = connect(args.host, args.port, unix_socket)
    try:
        banner = recv_line(sock)  # mesajul "OK connected" trimis de server la conectare
        print("<< " + banner, end="")

        if args.cmd == "ping":
            rc = cmd_ping(sock)
        elif args.cmd == "capacity":
            rc = cmd_capacity(sock, args.png_in)
        elif args.cmd == "validate":
            rc = cmd_validate(sock, args.png_in)
        elif args.cmd == "encode-text":
            rc = cmd_encode_text(sock, args.png_in, args.text, args.out_png)
        elif args.cmd == "encode-file":
            rc = cmd_encode_file(sock, args.png_in, args.file_in, args.out_png)
        elif args.cmd == "decode":
            rc = cmd_decode(sock, args.png_in, args.out_path)
        else:
            rc = 1

        send_line(sock, "QUIT")
        expect_line(sock)
    finally:
        sock.close()
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
