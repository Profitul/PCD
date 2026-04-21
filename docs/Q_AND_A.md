# Q&A Pregatire prezentare Milestone 1 - StegaPNG

Intrebari probabile + raspunsuri tehnice ancorate pe proiect. Intrebarile reprezinta ~60% din nota, asa ca partea asta e critica.

## 1. poll() vs select() vs epoll()
- **select**: bitmask FD_SET, limita FD_SETSIZE=1024, O(n) scan, trebuie recreat la fiecare apel.
- **poll**: array de `struct pollfd`, fara limita FD_SETSIZE, O(n), API mai curat.
- **epoll** (Linux-specific): kernel-side ready list, O(1) pe eveniment, scalabil la zeci de mii de conexiuni.
- **Alegerea noastra: poll**. Motiv: MAX_CLIENTS=128, poll e mai portabil decat epoll (spec cere POSIX), fara limitele select. Refacem vectorul in fiecare iteratie via `rebuild_pollfds()`.

## 2. pthread: mutex vs spinlock vs rwlock
- **mutex**: kernel sleep daca e locked, cost context switch; potrivit pentru sectiuni "lungi".
- **spinlock**: busy-wait, userspace; doar pentru sectiuni foarte scurte si contention mic.
- **rwlock**: mai multi cititori SAU un singur scriitor; bun la read-heavy.
- **Noi folosim mutex** pe `job_queue`, `job_table`, `blocklist`, `logger`. Sectiunile sunt suficient de "grele" (alocari, snprintf, I/O) ca mutex-ul sa fie alegerea corecta.

## 3. Condition variables
- **De ce pthread_cond_wait cere mutex**: atomic unlock+sleep, atomic re-lock la wakeup; fara mutex ar exista race intre "check conditie" si "sleep".
- **Spurious wakeups**: wait-ul trebuie intr-un `while(\!cond)`, nu `if(\!cond)`.
- **In queue.c**: `job_queue_push` face `pthread_cond_signal`, `worker_main` face `while(empty) pthread_cond_wait(&not_empty, &mutex)`.

## 4. Race condition, deadlock, starvation
- **Race**: acces concurent la date fara sincronizare (ex: doi workeri pop dintr-o coada nepazita).
- **Deadlock** (conditiile Coffman): mutual exclusion + hold-and-wait + no preemption + circular wait. Evitare: lock ordering, try-lock cu timeout, un singur mutex pe resursa.
- **Starvation**: un thread nu obtine niciodata resursa. Evitare: FIFO/fair locks.
- **In proiect**: folosim mereu **acelasi ordering** (lock queue -> push -> unlock), nu tinem doua mutex-uri simultan, folosim cond var pentru wake-up ordonat.

## 5. fork() vs thread
- **fork()**: proces nou, spatiu de adrese separat (copy-on-write), fail-safe (crash-ul unui child nu omoara parintele), cost mai mare la pornire.
- **thread**: shared memory, creare ieftina, crash intr-un thread = crash tot procesul.
- **Alegerea noastra**: workeri pe **thread** (shared coada + job_table via mutex), dar **fork()+exec()** in admin pentru export log. Motivatie: workerii au nevoie de acces la structuri partajate; export-ul de log e izolat si foloseste binarul extern `tar`.

## 6. Pipe anonim vs named pipe (FIFO) vs socketpair
- **pipe anonim** (`pipe()`): unidirectional, intre parent-child sau intre thread-uri.
- **named pipe** (`mkfifo`): persistent in filesystem, intre procese fara relatie.
- **socketpair**: bidirectional, mai flexibil.
- **Noi folosim pipe anonim** ca notificare worker->main loop: workerul scrie `worker_event_t` in pipe cand termina un job, main loop-ul il citeste via poll()+nonblock read. Non-bloking pe read-end.

## 7. TCP vs UDP
- **TCP**: fiabil, ordonat, stream-based, 3-way handshake, flow control, congestion control.
- **UDP**: datagram, fara garantii, fara conexiune.
- **Alegerea noastra: TCP**. Motiv: transferam PNG-uri si fisiere mari (zeci de MB); avem nevoie de ordine + livrare garantata. UDP ar cere reimplementare ACK/reordering.
- **SO_REUSEADDR**: il setam ca sa evitam TIME_WAIT blocarea portului la restart.
- **Nagle**: nu dezactivam (TCP_NODELAY off), mesajele noastre nu sunt latency-critical.

## 8. LSB steganografie
- **Principiu**: inlocuim bitul cel mai putin semnificativ al fiecarui byte de culoare (R/G/B) cu un bit din payload.
- **Capacitate**: pentru imagine WxH RGB 8-bit: `3 * W * H / 8` bytes (~1/8 din dimensiunea pixelilor).
- **Limitari**: fragil la recompresie JPEG (PNG ok pentru ca e lossless), detectabil statistic (chi-square, RS analysis) daca payload-ul nu e aleator.
- **Noi nu ascundem in canalul alpha** pentru a nu afecta transparenta; skip-uim canalul A.
- **Format payload**: magic 4 bytes (`STEG`) + version 1 byte + type 1 byte (text/file) + filename_len 2 bytes + filename + data_len 8 bytes + data + CRC32 4 bytes.

## 9. libpng
- **De ce libpng**: parsing/encoding PNG manual ar fi cateva mii de linii (deflate, CRC chunk, filtering per row). libpng e de-facto standard.
- **Flux**: `png_create_read_struct` -> `png_init_io` -> `png_read_info` -> `png_set_expand` (normalizeaza la RGB/RGBA 8-bit) -> `png_read_image` pe row_pointers.
- **Riscuri**: libpng foloseste `setjmp/longjmp` pentru erori; trebuie handler setat cu `png_set_longjmp_fn`.

## 10. Signal handling in multi-thread
- **Problema**: `signal()` este per-process, nu per-thread.
- **Solutie**: blocam toate semnalele in workeri (`pthread_sigmask`), las main thread-ul sa gestioneze SIGINT/SIGTERM. Alternativ: `signalfd()` pentru integrare in poll().
- **Noi**: `sigaction` pe SIGINT/SIGTERM in main, handler seteaza `volatile sig_atomic_t g_running = 0`. `SIGPIPE` il ignoram (altfel write-ul spre client deconectat ar omori procesul).

## 11. Producer-consumer pattern
- Coada thread-safe: `pthread_mutex_t` pentru mutual exclusion, `pthread_cond_t not_empty` pentru consumatori, optional `not_full` daca bounded.
- **In proiect**: `job_queue_t` e FIFO; `job_queue_push` = producer (main loop), `job_queue_pop` = consumer (worker threads).

## 12. Shutdown controlat
- **Probleme daca ucizi brutal**: file descriptor leaks, job-uri in zbor neterminate, log-uri netrimise, lock-uri ramase locked (daca e proces cloned).
- **Strategia noastra**:
  1. Semnal -> `g_running = 0`
  2. Main loop iese din poll
  3. `job_queue_stop()` seteaza flag + broadcast cond var
  4. Workerii detecteaza stop si ies
  5. `pthread_join` pe toti workerii
  6. Inchid file descriptori (socketi, pipe)
  7. Distrug mutex-uri si cond vars
  8. Inchid logger-ul

## 13. Stack vs heap
- **Stack**: alocare automata, rapid, limitat (~8MB tipic pe Linux), cleanup automat la iesire din functie.
- **Heap**: `malloc/free`, mai mare, lifecycle manual, risc leak/use-after-free.
- **In proiect**: `server_state_t clients[MAX_CLIENTS]` pe stack in `server_run()` (~10KB). Job-urile sunt pe heap (`job_table_create_job`) cu `free` in `job_table_destroy`.

## 14. CRC32 si integritate
- **De ce CRC32**: detecteaza corupere la citire/scriere, rapid, 32 biti sunt suficienti pentru payload mic.
- **Nu previne atacuri** (nu e cryptographic hash); pentru asta am nevoie SHA-256+HMAC.
- **In proiect**: calculez CRC32 peste header+data la encode, verific la decode. Daca CRC nu match -> `STEGO_ERR_CRC`.

## 15. Ce face libconfig in proiect
- Citeste `config/server.conf` (format libconfig) la startup.
- Precedenta: defaults compilate -> libconfig -> env (`STEGA_*`) -> CLI (`--port`, `--workers`...).
- API: `config_init` -> `config_read_file` -> `config_lookup_int/string` -> `config_destroy`.
- Fallback: daca libconfig lipseste, build-ul merge dar --config e disabled (avertisment).

## 16. getopt_long + env vars + uname
- `getopt_long`: parser standard GNU pentru `--long-option` + `-short`; gestioneaza automat argumente required/optional.
- Env via `getenv("STEGA_USER_PORT")` etc; parse cu `strtol`.
- `uname(struct utsname *)` -> sysname/nodename/release/machine -> util la debug per masina.
- In log apare: `Env: sys=Linux node=... rel=... mach=x86_64` + `pid=X uid=Y user=Z`.

## 17. fork()+exec()+wait() concret
- `fork()` returneaza 0 in child, pid_child in parent, -1 eroare.
- In child: `execl("/bin/tar", "tar", "czf", ...)` inlocuieste imaginea procesului.
- `_exit(127)` daca execl esueaza (nu `exit()` care face flush stdio buffers).
- Parent: `waitpid(pid, &status, 0)` blocheaza pana termina child; `WIFEXITED(status)` + `WEXITSTATUS(status)` pentru rezultat.
- In proiect: admin client -> meniu optiunea 13 "EXPORT LOG" -> fork -> execl tar -> wait.

## 18. Clang-tidy si analiza statica
- Folosim `gcc -fanalyzer` (echivalent simplificat): detecteaza use-after-free, double-free, null-deref, leak.
- Target `make analyze` compileaza fiecare fisier cu `-fanalyzer` + flag-urile stricte.
- Zero warnings pe tot codul.

## 19. Coding style strict
- `-Wall -Wextra -Wpedantic -Werror`: orice warning opreste build-ul.
- `-std=c11` + `-D_POSIX_C_SOURCE=200809L`: standard C11 + API POSIX explicit.
- **Functii nesigure evitate**: `gets()`, `strcpy()`, `strcat()`, `sprintf()` - inlocuite cu `fgets`, `snprintf`.
- Cast-uri explicite pentru conversii; `(void)` pe return values ignorate deliberat.

## 20. Arhitectura in cifre
- **Limbaj**: C11 (server + client + admin), Python 3 (client alternativ).
- **Threads**: 1 main (poll) + 1..8 workeri + fiecare client serializat in main loop.
- **Socketi**: 2 listen (9090 user, 9091 admin) + `MAX_CLIENTS`=128 conexiuni.
- **Pipe anonim**: 1 (worker->main notificare).
- **Mutex-uri**: 3 principale (queue, job_table, blocklist) + 1 pe logger.
- **Cond vars**: 1 (queue.not_empty).
- **Linii de cod C**: ~2500. Structura modulara pe 12 fisiere server + 3 clienti.

---

**Sfat prezentare**: cand primesti o intrebare, incepe cu definitia generala (1-2 propozitii) apoi justifica alegerea concreta din proiect ("noi am ales X pentru ca..."). Asta scoate in evidenta ca intelegi contextul, nu doar tema.
