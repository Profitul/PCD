# StegaPNG - Server/Client de steganografie in imagini PNG

Proiect PCD (Programarea Concurenta si Distributiva), echipa 4 membri.

Target curent: Nivel A + Nivel B
socket UNIX/LOCAL, INotify, gateway REST si interfata admin ncurses

---

## 1. Structura proiectului

```text
include/                Headere comune: common, config, net, protocol, runtime_config, server, storage etc.
src/server/             Serverul C si modulele comune: net, protocol, logger, queue, worker, storage, stego, png_utils
src/client/             Client ordinar C
src/admin/              Client admin C + client admin Python ncurses
python_client/          Client ordinar Python
config/                 Configurare server: server.conf
scripts/                Scripturi demo/test: demo_basic, demo_concurrent, demo_admin, test_cancel, test_kick
tests/                  Test steganografie LSB: stego_test
docs/                   Documentatie: SRS, SDD, Q_AND_A
poza/                   Imagini de test pentru demo
storage/                Director runtime generat: uploads, results, temp
logs/                   Loguri runtime generate
```

Structura recomandata pentru commit pe GitHub este sa fie pastrate sursele, headerele, documentatia, scripturile, config-ul si imaginea/imaginile de test. Nu se urca executabile, loguri, fisiere generate in `storage/`, cache Python sau arhive temporare.

---

## 2. Dependente

Pe Ubuntu/Debian:

```bash
sudo apt update
sudo apt install gcc make pkg-config libpng-dev libconfig-dev python3
```

Optional, pentru clientul admin ncurses in Python:

```bash
python3 --version
```

Modulul `curses` este inclus de obicei in Python pe Linux. Pe Windows se recomanda rularea prin WSL sau container Linux.

---

## 3. Build

Din root-ul proiectului:

```bash
make clean
make prepare-runtime
make all
```

Comenzi utile:

```bash
make check-deps      # verifica gcc + libpng + libconfig daca exista
make test            # ruleaza stego_test
make analyze         # static analysis cu gcc -fanalyzer
```

Flag-uri stricte folosite la compilare:

```text
-Wall -Wextra -Wpedantic -Werror -g -std=c11 -D_POSIX_C_SOURCE=200809L -pthread
```

Executabile generate:

```text
./server
./client
./admin_client
./stego_test
```

---

## 4. Configurare server

Precedenta configurarii este:

```text
CLI > variabile de mediu > config/server.conf > valori default din config.h
```

### 4.1 Fisier libconfig

Fisier default: `config/server.conf`

```text
server: {
    user_port        = 9090;
    admin_port       = 9091;
    worker_count     = 3;
    unix_socket_path = "/tmp/stegapng_user.sock";
    log_path         = "logs/server.log";
    storage_root     = "storage";
    max_upload_mb    = 64L;
};
```

### 4.2 Variabile de mediu

```text
STEGA_USER_PORT
STEGA_ADMIN_PORT
STEGA_WORKERS
STEGA_LOG
STEGA_STORAGE
STEGA_CONFIG
STEGA_UNIX_SOCKET
```

### 4.3 Argumente CLI

```bash
./server --help
./server --version
./server --config config/server.conf
./server --port 9100 --admin-port 9101 --workers 4
./server --unix-socket /tmp/stegapng_user.sock
./server --storage storage_test --log logs/server.log
```

---

## 5. Rulare corecta

### Terminal 1: server

```bash
./server
```

Daca terminalul ramane blocat fara meniu, este normal. Serverul nu este consola interactiva; el asteapta clienti pe porturile/socketurile configurate.

Implicit:

```text
User INET:  127.0.0.1:9090
Admin INET: 127.0.0.1:9091
UNIX socket: /tmp/stegapng_user.sock
Log: logs/server.log
Storage: storage/
```

### Terminal 2: client C

```bash
./client ping
./client validate poza/IMG_0003.png
./client capacity poza/IMG_0003.png
./client encode-text poza/IMG_0003.png "mesaj secret" storage/temp/out.png
./client decode storage/temp/out.png storage/temp/decoded.txt
cat storage/temp/decoded.txt
```

### Terminal 2: client Python INET

```bash
python3 python_client/steg_client.py ping
python3 python_client/steg_client.py validate poza/IMG_0003.png
python3 python_client/steg_client.py capacity poza/IMG_0003.png
python3 python_client/steg_client.py encode-text poza/IMG_0003.png "hello" storage/temp/py_out.png
python3 python_client/steg_client.py decode storage/temp/py_out.png storage/temp/py_decoded.txt
```

### Terminal 2: client Python prin UNIX socket

```bash
python3 python_client/steg_client.py --unix ping
python3 python_client/steg_client.py --unix validate poza/IMG_0003.png
python3 python_client/steg_client.py --unix-socket /tmp/stegapng_user.sock ping
```

---

## 6. Admin client

### Admin C

```bash
./admin_client 127.0.0.1 9091
```

Comenzi disponibile in meniu:

```text
STATS
LISTJOBS
HISTORY
LISTCLIENTS
AVGDURATION
CANCEL <id>
KICK <fd>
BLOCKIP <ip>
UNBLOCKIP <ip>
PING
HELP
RAW
EXPORT LOG
```

`EXPORT LOG` foloseste `fork()` + `execl("/bin/tar", ...)` + `waitpid()` pentru arhivarea directorului `logs/`.

### Admin Python ncurses

```bash
python3 src/admin/admin_client_ncurses.py --host 127.0.0.1 --port 9091
```

Acest client este folosit pentru cerinta de interfata text de tip ncurses.

---

## 7. REST Gateway

Gateway-ul REST porneste separat si traduce cereri HTTP catre protocolul TCP al serverului.

Terminal 1:

```bash
./server
```

Terminal 2:

```bash
python3 rest_gateway.py --listen 127.0.0.1 --http-port 8080 --stega-host 127.0.0.1 --stega-port 9090
```

Test:

```bash
curl -X POST http://127.0.0.1:8080/api/ping
```

Endpoint-uri principale:

```text
POST /api/ping
POST /api/validate
POST /api/capacity
POST /api/encode-text
POST /api/encode-file
POST /api/decode
GET  /api/jobs/<id>/status
GET  /api/jobs/<id>/result
GET  /api/jobs/<id>/meta
GET  /api/jobs/<id>/download
```

---

## 8. Functionalitati implementate

### Server

- socket INET pentru clienti user;
- socket INET separat pentru admin;
- socket UNIX/LOCAL/FILE pentru client local;
- `poll()` pentru listen sockets, clienti activi, pipe worker si INotify;
- thread pool pentru procesare asincrona;
- coada FIFO cu mutex si condition variable;
- job IDs, `STATUS`, `RESULT`, `META`, `DOWNLOAD`;
- transfer fisiere bidirectional pentru PNG/payload/output;
- pipe anonim de notificare worker -> server;
- INotify pe directoarele de runtime (`uploads`, `results`, `temp`);
- logging thread-safe;
- configurare prin libconfig/env/CLI;
- integrare `libpng` pentru procesare PNG/steganografie LSB.

### Clienti

- client C ordinar;
- client Python ordinar INET + UNIX socket;
- client admin C cu meniu interactiv;
- client admin Python cu ncurses;
- gateway REST ca interfata S/R.

---

## 9. Troubleshooting

### `listen user: Address already in use`

Portul 9090 este deja folosit. Verifica:

```bash
ss -ltnp | grep 909
ps aux | grep server
```

Opreste serverul vechi:

```bash
pkill -f "./server"
rm -f /tmp/stegapng_user.sock
```

sau porneste pe alte porturi:

```bash
./server --port 10090 --admin-port 10091 --unix-socket /tmp/stegapng_user2.sock
python3 python_client/steg_client.py --port 10090 ping
./admin_client 127.0.0.1 10091
```

### Serverul nu afiseaza nimic

Este normal. Serverul ruleaza si asteapta conexiuni. Foloseste alt terminal pentru client/admin.

### Verificare socketuri active

```bash
ss -ltnp | grep 909
ls -l /tmp/stegapng_user.sock
```

### Verificare loguri

```bash
tail -f logs/server.log
```

---

## 10. Demo

```bash
bash scripts/demo_basic.sh
bash scripts/demo_concurrent.sh
bash scripts/demo_admin.sh
bash scripts/test_cancel.sh
bash scripts/test_kick.sh
```

---

## 11. Ce se urca pe GitHub

Relevant pentru commit:

```text
include/
src/
python_client/steg_client.py
rest_gateway.py
config/server.conf
scripts/
tests/
docs/*.md
docs/*.pdf
poza/IMG_0003.png
Makefile
Dockerfile
docker-compose.yml
README.md
RUN_CLEAN.md
.gitignore
.dockerignore
```

Nu se urca:

```text
server, client, admin_client, stego_test
*.o, *.d
logs/*.log
storage/uploads/*
storage/results/*
storage/temp/*
storage/demo*
storage_runtime_test/
python_client/__pycache__/
*.pyc
*.zip
```

---

## 12. Impartire pe 4 membri

| Membru | Responsabilitati principale | Fisiere |
|-------:|-----------------------------|---------|
| Tamasila Vlad | Server core + networking + protocol | `src/server/server.c`, `net.c`, `protocol.c`, `include/server.h`, `protocol.h`, `net.h` |
| Tarniceriu Luca | Coada, workeri, job table, logger, runtime_config | `src/server/queue.c`, `worker.c`, `job.c`, `logger.c`, `runtime_config.c`, `include/job.h`, `logger.h`, `runtime_config.h` |
| Suta Georgian | Modul stego + integrare libpng + storage | `src/server/stego.c`, `png_utils.c`, `storage.c`, `include/stego.h`, `storage.h`, `tests/stego_test.c` |
| Albu David | Clienti, admin, REST, scripturi demo, documentatie | `src/client/main.c`, `src/admin/main.c`, `src/admin/admin_client_ncurses.py`, `python_client/`, `rest_gateway.py`, `scripts/`, `docs/`, `config/` |

Toti 4 colaboreaza pe `Makefile`, `include/common.h`, `include/config.h`, SRS/SDD si prezentare.

---

## 13. Niveluri acoperite

- Nivel A: socket INET, poll, thread pool, coada FIFO, transfer fisiere, logging, admin separat, clienti multipli.
- Nivel B: sincronizare cu mutex/cond, pipe anonim, semnale, cancel, kick, blocklist IP, configurare avansata.
- Extensii pentru punctaj maxim: socket UNIX/LOCAL, INotify, REST gateway, interfata admin ncurses.
