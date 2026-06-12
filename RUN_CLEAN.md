# Rulare curata StegaPNG

Toate comenzile de mai jos se ruleaza din root-ul proiectului, adica din directorul care contine `Makefile`, `src/`, `include/`, `python_client/`.

## 1. Curatare si compilare

```bash
make clean
make prepare-runtime
make all
```

Daca vezi mesajul:

```text
NOTE: libconfig NOT found
```

nu este eroare fatala. Serverul compileaza fara incarcarea fisierului `config/server.conf`. Pentru suport config file instaleaza `libconfig-dev` si ruleaza din nou `make clean && make all`.

## 2. Pornire server

In terminalul 1:

```bash
./server
```

Serverul nu are meniu in consola. Daca nu afiseaza nimic si terminalul ramane blocat, inseamna ca ruleaza si asteapta clienti.

Pentru configurare explicita:

```bash
./server --port 9090 --admin-port 9091 --unix-socket /tmp/stegapng_user.sock --storage storage --log logs/server.log
```

## 3. Verificare server

In alt terminal:

```bash
ss -ltnp | grep 909
ls -l /tmp/stegapng_user.sock
tail -f logs/server.log
```

Trebuie sa vezi porturile 9090/9091 si socketul UNIX `/tmp/stegapng_user.sock`.

## 4. Client C

```bash
./client ping
./client validate poza/IMG_0003.png
./client capacity poza/IMG_0003.png
./client encode-text poza/IMG_0003.png "mesaj secret" storage/temp/out_text.png
./client decode storage/temp/out_text.png storage/temp/decoded.txt
cat storage/temp/decoded.txt
```

## 5. Client Python TCP/INET

```bash
python3 python_client/steg_client.py ping
python3 python_client/steg_client.py validate poza/IMG_0003.png
python3 python_client/steg_client.py capacity poza/IMG_0003.png
python3 python_client/steg_client.py encode-text poza/IMG_0003.png "mesaj secret" storage/temp/out_py.png
python3 python_client/steg_client.py decode storage/temp/out_py.png storage/temp/decoded_py.txt
cat storage/temp/decoded_py.txt
```

## 6. Client Python UNIX/LOCAL socket

Ambele variante sunt valide:

```bash
python3 python_client/steg_client.py --unix ping
python3 python_client/steg_client.py --unix-socket ping
```

Cu path explicit:

```bash
python3 python_client/steg_client.py --unix-socket /tmp/stegapng_user.sock ping
```

## 7. Client admin C

In alt terminal:

```bash
./admin_client 127.0.0.1 9091
```

Sau:

```bash
make run-admin
```

## 8. Client admin ncurses Python

```bash
python3 src/admin/admin_client_ncurses.py --host 127.0.0.1 --port 9091
```

## 9. REST gateway

In terminalul 1 serverul C ramane pornit.
In terminalul 2 pornesti gateway-ul:

```bash
python3 rest_gateway.py --listen 127.0.0.1 --http-port 8080 --stega-host 127.0.0.1 --stega-port 9090
```

Test REST din terminalul 3:

```bash
curl -X POST http://127.0.0.1:8080/api/ping
```

## 10. Oprire server

In terminalul unde ruleaza serverul:

```text
Ctrl + C
```

Sau din alt terminal:

```bash
pkill -x server
```

## Probleme frecvente

1. `Address already in use`: serverul este deja pornit. Opreste-l cu `pkill -x server` sau foloseste alte porturi.
2. `logger_init(logs/server.log) failed`: ruleaza `make prepare-runtime`. Varianta curenta creeaza automat directorul `logs`, dar comanda ramane utila.
3. `connect: Connection refused`: serverul nu ruleaza sau ruleaza pe alt port.
4. `No such file or directory` la PNG: ruleaza comenzile din root-ul proiectului si foloseste `poza/IMG_0003.png`.
5. `libconfig NOT found`: nu e fatal. Daca vrei config file real, instaleaza `sudo apt install libconfig-dev`.
