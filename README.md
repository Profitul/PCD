# StegaPNG - Server-Client de steganografie in imagini PNG

Proiect PCD (Programarea Concurenta si Distribuita), echipa 4 membri.
Target: Nivel A + B

## Structura
```
include/           - headere comune (common, config, runtime_config, net, protocol, logger, job, storage, stego, server)
src/server/        - server + lib comuna (net/protocol/logger/queue/worker/storage/stego/png_utils/runtime_config)
src/client/        - client ordinar C
src/admin/         - admin client C (meniu interactiv + fork/wait export log)
python_client/     - client ordinar Python
tests/             - stego_test (smoke test roundtrip LSB)
scripts/           - demo_basic.sh, demo_concurrent.sh, demo_admin.sh, test_cancel.sh, test_kick.sh
docs/              - SRS.md, SDD.md, Q_AND_A.md (+ PDF-uri)
config/            - server.conf (libconfig)
poza/              - imagini de test
storage/           - uploads, results, temp (generate la runtime)
logs/              - server.log, demo logs
```

## Dependente
```
sudo apt install gcc make libpng-dev libconfig-dev
```

## Build
```
make check-deps         # verifica libpng + libconfig
make                    # server + client + admin_client
make test               # stego_test (smoke LSB)
make analyze            # static analysis cu gcc -fanalyzer
```
Flag-uri stricte: `-Wall -Wextra -Wpedantic -Werror -g -std=c11 -D_POSIX_C_SOURCE=200809L -pthread`.

## Configurare server (3 nivele, precedenta: CLI > env > libconfig > default)

### 1. Fisier libconfig (default: `config/server.conf`)
```
server: {
    user_port    = 9090;
    admin_port   = 9091;
    worker_count = 3;
    log_path     = "logs/server.log";
    storage_root = "storage";
    max_upload_mb = 64L;
};
```

### 2. Variabile de mediu
`STEGA_USER_PORT`, `STEGA_ADMIN_PORT`, `STEGA_WORKERS`, `STEGA_LOG`, `STEGA_STORAGE`, `STEGA_CONFIG`.

### 3. Argumente linie comanda
```
./server --help
./server --config config/server.conf --port 9100 --admin-port 9101 --workers 4 --log /tmp/s.log
./server -c config/server.conf -p 9100 -a 9101 -w 4
./server --version
```

La startup, serverul logheaza informatii de mediu:
- `uname()` (sys/node/release/machine)
- `pid`, `uid`, `user`
- `PATH_len`, `LANG`
- sumarul de config (inclusiv sursa fisierului)

## Rulare
```
./server                                            # user :9090, admin :9091
./client encode-text cover.png "secret" out.png
./client decode out.png out.txt
./client encode-file cover.png payload.bin out.png
./client decode out.png out.bin
./client capacity cover.png
./client validate cover.png
./client ping
python3 python_client/steg_client.py encode-text cover.png "hello" out.png
./admin_client                                      # meniu interactiv
```

## Admin client - comenzi
1) STATS, 2) LISTJOBS, 3) HISTORY, 4) LISTCLIENTS, 5) AVGDURATION
6) CANCEL `<id>`, 7) KICK `<fd>`, 8) BLOCKIP, 9) UNBLOCKIP
10) PING, 11) HELP, 12) RAW
13) **EXPORT LOG** - `fork()` + `execl("/bin/tar", ...)` + `waitpid()` pentru arhivare `logs/`

## Demo
```
bash scripts/demo_basic.sh
bash scripts/demo_concurrent.sh
bash scripts/demo_admin.sh
bash scripts/test_cancel.sh
bash scripts/test_kick.sh
```

## Impartire pe 4 membri (propunere)
| Membru | Responsabilitati principale | Fisiere |
|-------:|-----------------------------|---------|
| Tamasila Vlad | Server core + networking + protocol | `src/server/server.c`, `net.c`, `protocol.c`, `include/server.h`, `protocol.h`, `net.h` |
| Tarniceriu Luca | Coada, workeri, job table, logger, runtime_config | `src/server/queue.c`, `worker.c`, `job.c`, `logger.c`, `runtime_config.c`, `include/job.h`, `logger.h`, `runtime_config.h` |
| Suta Georgian | Modul stego + integrare libpng + storage | `src/server/stego.c`, `png_utils.c`, `storage.c`, `include/stego.h`, `storage.h`, `tests/stego_test.c` |
| Albu David | Clientii (C, Python), admin client, scripturi demo, documentatie | `src/client/main.c`, `src/admin/main.c`, `python_client/`, `scripts/`, `docs/`, `config/` |

Toti 4 colaboreaza pe: Makefile, `include/common.h` si `config.h`, SRS/SDD, prezentare.

## Nivele
- **Nivel A**: poll + thread pool + coada FIFO + transfer fisiere mari + logging + admin separat + 2 tipuri clienti.
- **Nivel B**: ID unic job, STATUS asincron, pipe anonim, cancel, kick, blocklist IP.
- **Extra M1**: libconfig + getopt_long + env vars + uname() + fork()/wait()+exec() in admin.
