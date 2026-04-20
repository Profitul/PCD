# StegaPNG - Server-Client de steganografie in imagini PNG

Proiect PCD (Programare Concurenta si Distribuita), echipa 4 membri.
Target: Nivel A + B, fara REST.

## Structura
```
include/           - headere comune (common, config, net, protocol, logger, job, storage, stego, server)
src/server/        - server + lib comuna (net/protocol/logger/queue/worker/storage/stego/png_utils)
src/client/        - client ordinar C
src/admin/         - admin client C (meniu interactiv)
python_client/     - client ordinar Python
tests/             - stego_test (smoke test roundtrip LSB)
scripts/           - demo_basic.sh, demo_concurrent.sh, demo_admin.sh
docs/              - SRS.md, SDD.md
poza/              - imagini de test
storage/           - uploads, results, temp (generate la runtime)
logs/              - server.log, demo logs
```

## Build
```
make                    # server + client + admin_client
make test               # stego_test (smoke LSB)
```
Flag-uri: `-Wall -Wextra -Wpedantic -Werror -g -std=c11 -D_POSIX_C_SOURCE=200809L -pthread`.
Dependente: `libpng-dev`, `libc`, `pthread`.

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

## Demo
```
bash scripts/demo_basic.sh
bash scripts/demo_concurrent.sh
bash scripts/demo_admin.sh
```

## Impartire pe 4 membri (propunere)
| Membru | Responsabilitati principale | Fisiere |
|-------:|-----------------------------|---------|
| Tamasila Vlad | Server core + networking + protocol | `src/server/server.c`, `net.c`, `protocol.c`, `include/server.h`, `protocol.h`, `net.h` |
| Tarniceriu Luca | Coada, workeri, job table, logger | `src/server/queue.c`, `worker.c`, `job.c`, `logger.c`, `include/job.h`, `logger.h` |
| Suta Georgian | Modul stego + integrare libpng + storage | `src/server/stego.c`, `png_utils.c`, `storage.c`, `include/stego.h`, `storage.h`, `tests/stego_test.c` |
| Albu David | Clientii (C, Python), admin client, scripturi demo, documentatie | `src/client/main.c`, `src/admin/main.c`, `python_client/`, `scripts/`, `docs/` |

Toti 4 colaboreaza pe: Makefile, `include/common.h` si `config.h`, SRS/SDD, prezentare.

## Nivele
- **Nivel A**: poll + thread pool + coada FIFO + transfer fisiere mari + logging + admin separat + 2 tipuri clienti.
- **Nivel B**: ID unic job, STATUS asincron, pipe anonim, cancel, kick, blocklist IP.
