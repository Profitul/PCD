# SDD - Software Design Document
# StegaPNG - Server de steganografie pe imagini PNG

Versiune: 1.0
Data: aprilie 2026
Materia: PCD, UVT

---

## 1. Introducere

### 1.1 Scop
Acest document descrie arhitectura interna si deciziile de design pentru
implementarea sistemului **StegaPNG** (vezi SRS.md pentru cerinte).

### 1.2 Vedere de ansamblu
Sistemul urmareste un model simplu server-centric: serverul in C gestioneaza
multiplex I/O, coada de job-uri si thread pool-ul de workeri; clientii
(C si Python) trimit imagini si primesc rezultate prin acelasi protocol
text+binar; admin-ul foloseste un port separat pentru comenzi de control.

---

## 2. Arhitectura sistemului

### 2.1 Diagrama logica
```
+-------------+       +---------------------------------------+
|  client C   |------>|               Server C                |
|  client Py  |  TCP  |  +---------+    +----------------+    |
+-------------+       |  | poll()  |--->|  queue (FIFO)  |--->+---+
                      |  +---------+    +----------------+    | W |
+-------------+  TCP  |       ^                 ^             | O |
|  admin C    |------>|       |   pipe anonim   |             | R |
+-------------+       |       +-----------------+             | K |
                      |  +-----------+   +--------+   +----+  | E |
                      |  |  logger   |   | jobs   |   | bl |  | R |
                      |  +-----------+   +--------+   +----+  | S |
                      +---------------------------------------+---+
                                         |
                                         v
                                 +-----------------+
                                 |  libpng + LSB   |
                                 +-----------------+
```

### 2.2 Componente principale
- **Main thread** - initializare, signal handling, loop `poll()`.
- **Networking** - listen user + listen admin, accept, read_line/read_exact.
- **Protocol parser** - transforma comenzile text in `protocol_request_t`.
- **Job queue** - FIFO cu mutex + condition variable.
- **Job table** - metadata (id, state, path-uri, owner_fd, owner_ip, timpi).
- **Worker pool** - N thread-uri consumatoare; per job apeleaza modulul stego.
- **Stego module** - libpng decode -> LSB embed/extract -> libpng encode.
- **Storage** - I/O pe disc pentru upload-uri si rezultate.
- **Logger** - fisier de log protejat de mutex.
- **Blocklist** - lista IP-uri refuzate la accept().
- **Notification pipe** - worker scrie un byte, main se trezeste din `poll`.

### 2.3 Structura de fisiere
```
stegapng-server/
  include/            - header-e publice comune
  src/
    server/           - cod server + biblioteci comune (net, protocol, ...)
    client/           - client ordinar C
    admin/            - client admin C
  python_client/      - client alternativ in Python
  storage/
    uploads/          - PNG-uri primite
    results/          - rezultate generate
    temp/             - fisiere intermediare
  logs/               - fisiere de log
  docs/               - SRS, SDD
  scripts/            - scripturi demo (basic, concurrent, admin)
  tests/              - test-uri dedicate (ex. stego_test)
  poza/               - imagini sample
  Makefile
```

---

## 3. Proiectarea concurentei

### 3.1 Model general
- 1 thread principal (poll + accept + protocol).
- N thread-uri worker (implicit 4).
- 1 mutex + 1 condition variable pe coada de job-uri.
- 1 mutex pe tabela de job-uri.
- 1 mutex pe logger.
- 1 mutex pe blocklist.
- 1 pipe anonim (read end in poll-ul thread-ului principal, write end folosit de worker
  cand un job isi schimba starea si main-ul trebuie sa actualizeze clientul).

### 3.2 Ciclul unui job (Nivel B)
1. Client trimite comanda + date binare.
2. Main thread valideaza, salveaza fisierele in `storage/uploads/`, creeaza un
   job in tabela cu id unic si stare QUEUED; trimite `JOB <id>` clientului.
3. Main pune job-ul in coada (`queue_push`) si semnalizeaza cond var.
4. Un worker preia job-ul, trece starea in RUNNING, apeleaza modulul stego.
5. Worker scrie rezultatul in `storage/results/job_<id>.{png,txt,bin}`,
   seteaza starea in DONE/FAILED si timpii.
6. (Opt) worker scrie 1 byte in notification pipe pentru a trezi main.
7. Clientul face `STATUS <id>` pana primeste DONE, apoi `META` + `DOWNLOAD`.

### 3.3 Evitarea race conditions si deadlock-urilor
- Coada: mutex in jurul push/pop; cond var pentru notificare.
- Tabela job-uri: mutex dedicat, locat strict in jurul accesului la fiecare camp.
- Ordine consistenta de acquire (nu se ia vreodata tabela inainte sa eliberezi
  coada si invers; singurul caz critic e cancel, unde flag-ul se seteaza sub
  mutex-ul tabelei si workerul il citeste la inceputul fiecarei etape).
- Logger-ul are mutexul lui, independent.
- Ignorare SIGPIPE global; fiecare `write` verifica rc.

### 3.4 Shutdown controlat
- Handler pentru SIGINT/SIGTERM seteaza un flag `atomic_bool`.
- Main-ul iese din `poll`, trimite `stop_signal` la workeri (pun un sentinel in
  coada), asteapta `pthread_join` pe fiecare, inchide logger + socket-uri.

---

## 4. Protocol

### 4.1 Principii
- Comenzi in text ASCII, un verb pe linie (`COMMAND arg1 arg2 ... \n`).
- Payload binar declarat prin marimi explicite, urmeaza imediat dupa comanda.
- Raspunsurile sunt linii tip `OK ...` / `ERR ...` / `JOB <id>` / `DATA <size>`.
- Linii <= 1024 caractere.

### 4.2 Exemple
```
Client ->  ENCODE_TEXT 12345 14\n<12345 bytes PNG><14 bytes text>
Server <-  JOB 1\n

Client ->  STATUS 1\n
Server <-  STATUS 1 DONE\n

Client ->  DOWNLOAD 1\n
Server <-  DATA 12345\n<12345 bytes PNG>
```

### 4.3 Comenzi suportate
Vezi SRS.md cap. 3.2 / 3.3. Lista rapida:
- user: `PING`, `HELP`, `CAPACITY`, `VALIDATE`, `ENCODE_TEXT`, `ENCODE_FILE`,
  `DECODE`, `STATUS`, `META`, `RESULT`, `DOWNLOAD`, `QUIT`.
- admin: `PING`, `HELP`, `STATS`, `AVGDURATION`, `LISTJOBS`, `HISTORY`,
  `LISTCLIENTS`, `CANCEL`, `KICK`, `BLOCKIP`, `UNBLOCKIP`, `QUIT`.

---

## 5. Modulul stego

### 5.1 Format intern
```
[STG1][type:1][name_len:2 BE][name...][payload_len:4 BE][payload...][CRC32:4 BE]
```
CRC32 este calculat peste tot bufferul de la magic pana la capatul payload-ului
(fara sa includa CRC32-ul insusi). Tabel precomputat pentru eficienta.

### 5.2 Tehnica LSB
- Ruleaza pe bit-urile de ordin 0 din fiecare canal R/G/B.
- La RGBA, canalul alpha (index 3 modulo 4) este pastrat neschimbat.
- 1 byte de payload = 8 canale consecutive.
- Capacitate = `(W * H * data_channels) / 8`.

### 5.3 Pipeline libpng
Decodare: `png_create_read_struct` -> `png_read_info` -> transformari (palette->RGB,
gray->RGB, strip 16) -> `png_read_image`.
Encodare: `png_create_write_struct` -> `png_set_IHDR` -> `png_write_image`
(color type pastrat, bit depth 8).

### 5.4 Functii publice
```c
int stego_get_capacity(const char *png, stego_capacity_t *out);
int stego_encode_text(const char *in, const char *out, const char *text, size_t n);
int stego_encode_file(const char *in, const char *out, const char *payload, const char *name);
int stego_decode(const char *png, stego_extracted_t *out);
void stego_extracted_free(stego_extracted_t *e);
```

### 5.5 Erori posibile
`STEGO_OK`, `STEGO_ERR_OPEN`, `STEGO_ERR_NOT_PNG`, `STEGO_ERR_UNSUPPORTED`,
`STEGO_ERR_MEMORY`, `STEGO_ERR_CAPACITY`, `STEGO_ERR_LIBPNG`, `STEGO_ERR_MAGIC`,
`STEGO_ERR_CRC`, `STEGO_ERR_IO`, `STEGO_ERR_ARG`.

---

## 6. Design detaliat al modulelor

### 6.1 net.c
- `read_line(fd, buf, size)` - citeste pana la `\n` sau EOF.
- `read_exact(fd, buf, n)` - citire blocking exact n bytes.
- `write_all(fd, data, n)` - garantat scrie n bytes.
- `discard_exact(fd, n)` - arunca n bytes (cand marimea e invalida).

### 6.2 queue.c / job.c
- `job_queue_t` - coada circulara cu `pthread_mutex_t` + `pthread_cond_t`.
- `job_table_t` - array static cu slot-uri, protejat de mutex.
- Cancel: `job_request_cancel` seteaza flag; worker verifica la inceput de faza.

### 6.3 worker.c
- Loop: `queue_pop_wait` -> dispatch pe `job_type`:
  - `JOB_TYPE_ENCODE_TEXT` / `_FILE` / `_DECODE` / `CAPACITY` / `VALIDATE`.
- Actualizeaza meta rezultat (result_kind, result_size, result_filename).

### 6.4 storage.c
- `storage_init_dirs()` - creeaza arborele.
- `storage_receive_to_file(fd, path, size)` - streaming socket -> disk.
- `storage_send_file(fd, path)` - streaming disk -> socket.

### 6.5 logger.c
- `logger_init(path)`, `logger_close()`, `logger_log(level, fmt, ...)`.
- Nivele: INFO, WARN, ERROR. Timestamp ISO local.

### 6.6 Server main
- `poll()` peste: listen_user, listen_admin, notify_pipe, clientii activi.
- La POLLIN pe listen: accept + evaluare blocklist + attach la poll set.
- La POLLIN pe client: read linie, apel `handle_user_command` /
  `handle_admin_command`.
- La `kick_requested` dupa iteratie: close si compactare.

---

## 7. Diagrame recomandate pentru prezentare
- Diagrama contextului (actori: user, admin, server, storage, libpng).
- Diagrama componentelor (asa cum apare in sectiunea 2.1).
- Diagrama secventei pentru `ENCODE_TEXT` (client -> main -> queue -> worker -> stego -> storage -> client).
- Diagrama starilor pentru `job_state_t` (QUEUED -> RUNNING -> DONE/FAILED/CANCELED).
- Diagrama modulului stego (libpng + container + CRC).

---

## 8. Build, testare, demonstratie

### 8.1 Build
```
make                # server + client + admin_client
make test           # stego_test (smoke LSB roundtrip)
```

### 8.2 Scripturi demo (in `scripts/`)
- `demo_basic.sh`      - ping, capacity, validate, encode/decode text, encode/decode file.
- `demo_concurrent.sh` - 4 clienti paraleli (2 C + 2 Python).
- `demo_admin.sh`      - STATS, LISTCLIENTS, LISTJOBS, BLOCKIP, UNBLOCKIP, HISTORY.

### 8.3 Prezentare
1. Build clean cu flag-uri stricte.
2. `make test` (stego modul).
3. `demo_basic.sh` (user flow).
4. `demo_concurrent.sh` (coada + workeri).
5. `demo_admin.sh` (admin + blocklist).
6. Consultare `logs/server_demo_*.log` pentru logging demonstrabil.

---

## 9. Justificarea alegerilor tehnologice
- **C + POSIX**: cerinta explicita, permite control fin pe socket-uri,
  pthreads, pipe anonim; zero dependente de VM.
- **libpng**: cerinta explicita pentru PNG; single point of truth pentru
  decodare + encodare.
- **TCP text+binar**: simplu de debugat (tcpdump/nc lizibil), dar suporta
  payload-uri binare arbitrare prin marimi explicite.
- **poll() vs select()**: poll lucreaza cu liste explicite si nu e limitat
  la FD_SETSIZE; mai clar cand numarul de clienti variaza.
- **pthread + mutex + condvar**: API-ul POSIX standard, cerut de proiect.
- **Python pentru al doilea client**: productiv pentru scripturi demo,
  rapid de scris, cu argparse si socket standard library.

---

## 10. Riscuri si mitigatii
- **Fisiere mari blocheaza workerul** - limitam la 64 MB si procesam streaming.
- **Client abandonat lasa fisiere** - job-urile au durata limita si fisierele pot fi sterse periodic (optional).
- **Race la cancel** - flag atomic citit la inceputul fiecarei faze a job-ului.
- **Log contention** - mutex unic pe logger; scriem pe linii scurte.
- **Deadlock** - ordonare stricta a lock-urilor si fara sa tinem > 1 lock in timp ce chemam I/O.
