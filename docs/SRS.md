# SRS - Software Requirements Specification
# StegaPNG - Server de steganografie pe imagini PNG

Versiune: 1.0
Data: aprilie 2026
Materia: Programarea Calculatoarelor si Distribuite (PCD), UVT
Echipa: 4 membri

---

## 1. Introducere

### 1.1 Scop
Acest document defineste cerintele functionale si nefunctionale pentru sistemul
client-server **StegaPNG**, destinat ascunderii si extragerii de informatii
(text sau fisiere) in imagini PNG folosind tehnica LSB (Least Significant Bit),
prin intermediul bibliotecii `libpng`.

### 1.2 Domeniu
Aplicatia permite mai multor clienti sa trimita imagini PNG catre un server
centralizat pentru operatii de steganografie. Serverul proceseaza cererile in
paralel folosind un thread pool, raporteaza status-ul job-urilor si
returneaza rezultatele. Un client administrativ dedicat ofera rapoarte si
actiuni de control.

### 1.3 Definitii si acronime
- **LSB** - Least Significant Bit
- **PNG** - Portable Network Graphics
- **FIFO** - First-In-First-Out
- **RGBA** - Red, Green, Blue, Alpha
- **CRC32** - Cyclic Redundancy Check pe 32 de biti
- **TCP** - Transmission Control Protocol
- **JOB** - unitate de lucru identificata printr-un ID unic, executata de worker

### 1.4 Referinte
- Libpng documentation (http://www.libpng.org/pub/png/libpng.html)
- POSIX.1-2008 (IEEE Std 1003.1-2008)
- Cerinte PCD - Nivel A + B (fara Nivel C, fara REST)

---

## 2. Descriere generala

### 2.1 Perspectiva produsului
Sistem client-server cu server nativ C pe Linux, clienti ordinari in C si
Python, client admin dedicat. Comunicare TCP pe doua porturi separate (user
si admin). Procesare imagini folosind libpng pentru decodare/encodare PNG.

### 2.2 Functii principale
1. Encode text intr-o imagine PNG.
2. Decode text/fisier ascuns intr-o imagine PNG steganografica.
3. Encode fisier binar arbitrar intr-o imagine PNG.
4. Analiza capacitate (cat payload incape intr-o imagine).
5. Validare imagine PNG.
6. Administrare: rapoarte, cancel job, kick client, block/unblock IP.

### 2.3 Caracteristicile utilizatorilor
- **User** - trimite imagini si payload, primeste rezultat. Nu necesita
  autentificare in versiunea de baza. Conexiuni simultane multiple.
- **Admin** - conectat local, o singura instanta admin la un moment dat,
  executa comenzi de control si diagnostic.

### 2.4 Constrangeri
- Server obligatoriu in C pe UNIX/Linux.
- Folosire `libpng` pentru orice operatie PNG.
- Folosire `poll()` pentru I/O multiplex.
- Concurenta cu `pthread` (mutex + condition variable) si pipe anonim.
- Coding style strict: `-Wall -Wextra -Wpedantic -Werror -std=c11`.
- Imagini PNG 8-bit (RGB sau RGBA). Alte formate respinse.
- Dimensiune maxima transfer: 64 MB per fisier.

### 2.5 Presupuneri si dependente
- Clientul si serverul ruleaza pe retea de incredere (localhost in demo).
- Admin-ul ruleaza pe aceeasi masina cu serverul.
- Sistem Linux (dezvoltat/testat Ubuntu 22.04).

---

## 3. Cerinte functionale

### 3.1 Server
- **RF-01** Serverul deschide doua porturi TCP: unul pentru useri (SERVER_PORT)
  si unul pentru admin (ADMIN_PORT).
- **RF-02** Serverul foloseste `poll()` pentru multiplex I/O pe toti descriptorii.
- **RF-03** Serverul accepta maxim MAX_CLIENTS conexiuni user simultane.
- **RF-04** Serverul accepta maxim un singur admin la un moment dat.
- **RF-05** Toate cererile de procesare (user) intra intr-o coada FIFO
  protejata de mutex + condition variable.
- **RF-06** Thread-uri worker consuma coada si proceseaza job-urile.
- **RF-07** Fiecare job primeste un ID unic incremental si un state
  (QUEUED/RUNNING/DONE/FAILED/CANCELED).
- **RF-08** Serverul suporta reincarcarea PNG-ului de intrare si scrierea
  PNG-ului de iesire folosind libpng.
- **RF-09** Serverul logheaza evenimentele in `logs/server.log` cu timestamp si
  nivel (INFO/WARN/ERROR).
- **RF-10** Serverul refuza conexiunile de pe IP-urile din blocklist.
- **RF-11** Serverul suporta shutdown controlat la semnalele SIGINT/SIGTERM.

### 3.2 Comenzi user
- **RF-20** `PING` - raspuns `PONG`.
- **RF-21** `HELP` - lista comenzilor.
- **RF-22** `CAPACITY <png_size>` + binar PNG - raspunde cu capacitatea.
- **RF-23** `VALIDATE <png_size>` + binar PNG - raspunde daca este PNG valid.
- **RF-24** `ENCODE_TEXT <png_size> <text_size>` + PNG + text - creeaza job.
- **RF-25** `ENCODE_FILE <png_size> <name_size> <file_size>` + PNG + name +
  fisier - creeaza job.
- **RF-26** `DECODE <png_size>` + PNG - creeaza job de decodare.
- **RF-27** `STATUS <job_id>` - raporteaza starea unui job.
- **RF-28** `META <job_id>` - raporteaza tipul si dimensiunea rezultatului.
- **RF-29** `DOWNLOAD <job_id>` - livreaza `DATA <size>` + bytes.
- **RF-30** `RESULT <job_id>` - livreaza un rezumat scurt al rezultatului.
- **RF-31** `QUIT` - deconectare eleganta.

### 3.3 Comenzi admin
- **RF-40** `STATS` - totaluri job-uri + durata medie + nr. clienti.
- **RF-41** `LISTJOBS` - job-urile in curs.
- **RF-42** `HISTORY` - ultimele N job-uri finalizate.
- **RF-43** `LISTCLIENTS` - lista conexiunilor active (fd/tip/ip).
- **RF-44** `AVGDURATION` - durata medie a job-urilor terminate.
- **RF-45** `CANCEL <job_id>` - marcheaza un job pentru anulare.
- **RF-46** `KICK <fd>` - inchide fortat un client user.
- **RF-47** `BLOCKIP <ip>` - adauga IP in blocklist + kick conexiuni active.
- **RF-48** `UNBLOCKIP <ip>` - scoate IP din blocklist.

### 3.4 Modul stego
- **RF-60** Incapsulare payload: magic `STG1` + type (1B) + name_len (2B BE) +
  name + payload_len (4B BE) + payload + CRC32 (4B BE).
- **RF-61** Folosire numai a canalelor R/G/B (alpha pastrat intact pentru RGBA).
- **RF-62** Detectare erori CRC la decodare.
- **RF-63** Capacitate maxima = (W * H * data_channels) / 8 bytes.
- **RF-64** Imagini acceptate: PNG 8-bit, RGB sau RGBA.

---

## 4. Cerinte nefunctionale

### 4.1 Performanta
- **RNF-01** Timp de procesare < 2s pentru imagini tipice (sub 8 MP).
- **RNF-02** Suport transfer fisiere pana la 64 MB per cerere.
- **RNF-03** Chunk de transfer 64 KB pentru a evita memorie excesiva.

### 4.2 Robustete
- **RNF-10** Validare stricta a tuturor marimilor primite de la client.
- **RNF-11** Timeout implicit pe conexiuni inactive (via `poll`).
- **RNF-12** Protejare impotriva race-conditions prin `pthread_mutex_t`.
- **RNF-13** Ignorare SIGPIPE; gestionare eroare pe fiecare `write`.

### 4.3 Calitatea codului
- **RNF-20** Compilare fara warning cu flag-uri stricte.
- **RNF-21** Separare clara pe module: server, net, logger, job, queue, worker,
  storage, stego, protocol.
- **RNF-22** Functii <= 80 linii unde este realizabil.
- **RNF-23** Cod fara functii nesigure (evitare `gets`, `strcpy` neverificat,
  `sprintf` fara `n`).

### 4.4 Portabilitate
- **RNF-30** POSIX.1-2008, Linux x86_64.
- **RNF-31** Dependente: `libpng`, `pthread`, `libc`. Fara frameworks externe.

---

## 5. Cerinte de interfata

### 5.1 Interfete de retea
- TCP/IPv4, default `127.0.0.1:9090` (user) si `127.0.0.1:9091` (admin).
- Protocol hibrid text+binar: comenzi pe linii UTF-8, urmate de sectiuni binare
  de lungime declarata.

### 5.2 Interfete de stocare
- `storage/uploads/` - PNG-uri primite de la client.
- `storage/results/` - fisiere rezultate, denumite `job_<id>.{png,txt,bin}`.
- `storage/temp/` - scratchpad intermediar.
- `logs/server.log` - log-ul serverului.

---

## 6. Nivele acoperite
- **Nivel A**: poll + thread pool + transfer fisiere mari + coada FIFO
  comuna + logging + 2 tipuri de clienti + admin separat.
- **Nivel B**: ID unic pe job + STATUS asincron + pipe anonim
  (worker->main) + cancel + kick + blocklist IP.
- **Nivel C**: nu este urmarit.
- Componenta REST: nu este inclusa.
