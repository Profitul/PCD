#!/usr/bin/env python3
"""
Client admin ncurses pentru StegaPNG.

Operatii admin acoperite: PING, STATS, LISTJOBS, HISTORY, AVGDURATION,
LISTCLIENTS, CANCEL, KICK, BLOCKIP, UNBLOCKIP, HELP, QUIT.

Rulare:
    python3 admin_client_ncurses.py --host 127.0.0.1 --port 9091
"""
from __future__ import annotations

import argparse
import curses
import socket
from dataclasses import dataclass

DEFAULT_HOST = "127.0.0.1"
DEFAULT_ADMIN_PORT = 9091
SOCKET_TIMEOUT_S = 10.0
MAX_LINE_BYTES = 65536


@dataclass
class AdminConnection:
    sock: socket.socket

    @classmethod
    def connect(cls, host: str, port: int) -> tuple["AdminConnection", str]:
        sock = socket.create_connection((host, port), timeout=SOCKET_TIMEOUT_S)
        conn = cls(sock)
        banner = conn.recv_line()
        return conn, banner

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass

    def send_line(self, line: str) -> None:
        if not line.endswith("\n"):
            line += "\n"
        self.sock.sendall(line.encode("utf-8"))

    def recv_line(self) -> str:
        data = bytearray()
        while len(data) < MAX_LINE_BYTES:
            chunk = self.sock.recv(1)
            if not chunk:
                break
            data.extend(chunk)
            if chunk == b"\n":
                break
        return data.decode("utf-8", errors="replace").rstrip("\n")

    def command(self, line: str) -> str:
        self.send_line(line)
        return self.recv_line()


MENU = [
    ("1", "PING", "Verifica daca serverul admin raspunde"),
    ("2", "STATS", "Statistici joburi + clienti"),
    ("3", "LISTJOBS", "Lista joburilor curente"),
    ("4", "HISTORY", "Istoricul ultimelor joburi"),
    ("5", "AVGDURATION", "Durata medie a joburilor"),
    ("6", "LISTCLIENTS", "Lista clientilor conectati"),
    ("7", "CANCEL", "Anuleaza un job dupa id"),
    ("8", "KICK", "Deconecteaza un client dupa fd"),
    ("9", "BLOCKIP", "Blocheaza un IP si da kick clientilor existenti"),
    ("u", "UNBLOCKIP", "Deblocheaza un IP"),
    ("h", "HELP", "Comenzile acceptate de server"),
    ("q", "QUIT", "Inchide sesiunea admin"),
]

NEEDS_ARGUMENT = {
    "CANCEL": "job id",
    "KICK": "client fd",
    "BLOCKIP": "ip",
    "UNBLOCKIP": "ip",
}


def draw(stdscr: "curses._CursesWindow", host: str, port: int, banner: str, last_cmd: str, response: str) -> None:
    stdscr.erase()
    height, width = stdscr.getmaxyx()

    title = f"StegaPNG Admin TUI  |  {host}:{port}"
    stdscr.addnstr(0, 0, title, width - 1, curses.A_BOLD)
    stdscr.addnstr(1, 0, f"Banner: {banner}", width - 1)
    stdscr.hline(2, 0, curses.ACS_HLINE, max(0, width - 1))

    row = 3
    for key, cmd, desc in MENU:
        if row >= height - 7:
            break
        stdscr.addnstr(row, 0, f"[{key}] {cmd:<12} {desc}", width - 1)
        row += 1

    stdscr.hline(height - 6, 0, curses.ACS_HLINE, max(0, width - 1))
    stdscr.addnstr(height - 5, 0, f"Ultima comanda: {last_cmd}", width - 1, curses.A_BOLD)

    label = "Raspuns: "
    stdscr.addnstr(height - 4, 0, label, width - 1, curses.A_BOLD)
    # Afisam raspunsul pe max 3 linii, ca sa nu stricam layout-ul.
    text = response if response else "-"
    chunks = [text[i:i + max(1, width - 1)] for i in range(0, len(text), max(1, width - 1))]
    for i, chunk in enumerate(chunks[:3]):
        stdscr.addnstr(height - 3 + i, 0, chunk, width - 1)

    stdscr.refresh()


def prompt(stdscr: "curses._CursesWindow", question: str) -> str:
    height, width = stdscr.getmaxyx()
    curses.echo()
    stdscr.addnstr(height - 1, 0, " " * (width - 1), width - 1)
    stdscr.addnstr(height - 1, 0, f"{question}: ", width - 1, curses.A_BOLD)
    stdscr.refresh()
    try:
        raw = stdscr.getstr(height - 1, len(question) + 2, width - len(question) - 3)
        return raw.decode("utf-8", errors="replace").strip()
    finally:
        curses.noecho()


def run_tui(stdscr: "curses._CursesWindow", host: str, port: int) -> None:
    curses.curs_set(0)
    stdscr.keypad(True)

    conn, banner = AdminConnection.connect(host, port)
    last_cmd = "-"
    response = "Conectat. Alege o operatie."

    try:
        while True:
            draw(stdscr, host, port, banner, last_cmd, response)
            key = stdscr.getch()
            selected = None
            for menu_key, cmd, _desc in MENU:
                if key == ord(menu_key):
                    selected = cmd
                    break
            if selected is None:
                response = "Tasta necunoscuta. Alege o optiune din meniu."
                continue

            if selected == "QUIT":
                last_cmd = "QUIT"
                response = conn.command("QUIT")
                draw(stdscr, host, port, banner, last_cmd, response)
                break

            arg_name = NEEDS_ARGUMENT.get(selected)
            if arg_name is not None:
                curses.curs_set(1)
                arg = prompt(stdscr, f"Introdu {arg_name}")
                curses.curs_set(0)
                if not arg:
                    response = "Comanda anulata: argument gol."
                    continue
                cmd_line = f"{selected} {arg}"
            else:
                cmd_line = selected

            last_cmd = cmd_line
            try:
                response = conn.command(cmd_line)
            except OSError as exc:
                response = f"Eroare socket: {exc}"
    finally:
        conn.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Client admin ncurses pentru StegaPNG")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_ADMIN_PORT)
    args = parser.parse_args()

    curses.wrapper(run_tui, args.host, args.port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
