# Uruchamianie i Testowanie

## Kompilacja

### Wymagania

- CMake 3.16+
- Kompilator C++17 (GCC 7+, Clang 5+)
- System POSIX (Linux, macOS)

### Budowanie

```bash
# Utwórz katalog build
mkdir build && cd build

# Skonfiguruj CMake
cmake ..

# Skompiluj
make -j$(nproc)

# Lub dla pojedynczego rdzenia
make
```

### Wynikowe Pliki

```
build/
├── ropeway_simulation     # Główny program
├── tourist_process        # Proces turysty
├── cashier_process        # Proces kasy
├── lower_worker_process   # Pracownik dolnej stacji
├── upper_worker_process   # Pracownik górnej stacji
└── logger_process         # Proces loggera
```

---

## Konfiguracja

### Plik ropeway.env

```bash
# Kopiuj przykładowy plik
cp ropeway.env.example ropeway.env

# Edytuj według potrzeb
nano ropeway.env
```

### Podstawowe Parametry

```bash
# Liczba turystów do wygenerowania
ROPEWAY_NUM_TOURISTS=50

# Maksymalna pojemność dolnej stacji
ROPEWAY_STATION_CAPACITY=20

# Skala czasu (1 sekunda rzeczywista = X sekund symulowanych)
ROPEWAY_TIME_SCALE=600

# Godziny otwarcia (symulowane)
ROPEWAY_OPENING_HOUR=8
ROPEWAY_CLOSING_HOUR=18

# Całkowity czas symulacji w mikrosekundach
ROPEWAY_DURATION_US=60000000  # 60 sekund
```

### Parametry Czasowe

```bash
# Interwał głównej pętli (µs)
ROPEWAY_MAIN_LOOP_POLL_US=100000

# Opóźnienie między turystami (µs)
ROPEWAY_ARRIVAL_DELAY_BASE_US=100000
ROPEWAY_ARRIVAL_DELAY_RANDOM_US=200000

# Czas przejazdu krzesełkiem (µs)
ROPEWAY_RIDE_DURATION_US=2000000

# Czasy tras rowerowych (µs)
ROPEWAY_TRAIL_EASY_US=1000000
ROPEWAY_TRAIL_MEDIUM_US=2000000
ROPEWAY_TRAIL_HARD_US=3000000
```

### Parametry Biletów

```bash
# Prawdopodobieństwa typów biletów (%)
ROPEWAY_TICKET_SINGLE_USE_PCT=30
ROPEWAY_TICKET_TK1_PCT=25
ROPEWAY_TICKET_TK2_PCT=20
ROPEWAY_TICKET_TK3_PCT=15
# (Pozostałe 10% = DAILY)

# Czasy ważności biletów (sekundy symulowane)
ROPEWAY_TK1_DURATION_SEC=3600    # 1 godzina
ROPEWAY_TK2_DURATION_SEC=7200    # 2 godziny
ROPEWAY_TK3_DURATION_SEC=14400   # 4 godziny
ROPEWAY_DAILY_DURATION_SEC=36000 # 10 godzin
```

### Parametry Testowe

```bash
# Szansa na VIP (domyślnie 1%)
ROPEWAY_VIP_CHANCE_PCT=1

# Szansa na dzieci (domyślnie 20%)
ROPEWAY_CHILD_CHANCE_PCT=20

# Wymuś emergency stop po X sekundach (0 = losowo)
ROPEWAY_FORCE_EMERGENCY_AT_SEC=0
```

---

## Uruchamianie

### Podstawowe Uruchomienie

```bash
cd build
./ropeway_simulation
```

### Przykładowe Wyjście

```
============================================================
[08:00] [INFO ] [Simulation] Ropeway Simulation
============================================================
[08:00] [INFO ] [Simulation] Creating IPC...
[08:00] [DEBUG] [IpcManager] created
[08:00] [INFO ] [Simulation] All processes ready
[08:00] [INFO ] [Tourist 1] Buying ticket...
[08:00] [INFO ] [Cashier] Sold DAILY ticket to Tourist 1 (VIP)
[08:00] [INFO ] [Tourist 1] Waiting for entry gate...
...
```

### Zatrzymanie

- **Ctrl+C** - Graceful shutdown (SIGTERM)
- **Ctrl+Z** - Pauza symulacji (SIGTSTP), `fg` wznawia

---

## Testowanie

### Struktura Testów

```
tests/
├── configs/           # Konfiguracje dla testów
│   ├── test1_capacity.env
│   ├── test2_children.env
│   ├── test3_vip.env
│   └── test4_emergency.env
├── reports/           # Raporty z testów
└── run_tests.sh       # Skrypt uruchamiający
```

### Test 1: Limit Pojemności Stacji

**Cel:** Sprawdzić czy limit N osób na stacji nie jest przekraczany.

```bash
# Konfiguracja
ROPEWAY_NUM_TOURISTS=30
ROPEWAY_STATION_CAPACITY=5
ROPEWAY_DURATION_US=60000000
```

**Weryfikacja:**
- W logach nigdy nie pojawi się więcej niż 5 osób jednocześnie
- Turyści ponad limit czekają przed bramkami

### Test 2: Dzieci z Opiekunem

**Cel:** Sprawdzić czy dzieci <8 lat jadą tylko z dorosłym.

```bash
# Konfiguracja
ROPEWAY_NUM_TOURISTS=20
ROPEWAY_CHILD_CHANCE_PCT=100  # Każdy dorosły ma dzieci
ROPEWAY_DURATION_US=90000000
```

**Weryfikacja:**
- Każde dziecko ma guardianId w raporcie
- Dorosły ma max 2 dzieci

### Test 3: Priorytet VIP

**Cel:** Sprawdzić czy VIP-y wchodzą pierwsi.

```bash
# Konfiguracja
ROPEWAY_NUM_TOURISTS=100
ROPEWAY_VIP_CHANCE_PCT=10
ROPEWAY_STATION_CAPACITY=15
ROPEWAY_DURATION_US=120000000
```

**Weryfikacja:**
- VIP-y wchodzą przed zwykłymi turystami
- Brak zagłodzenia zwykłych turystów

### Test 4: Zatrzymanie Awaryjne

**Cel:** Sprawdzić protokół emergency stop/resume.

```bash
# Konfiguracja
ROPEWAY_NUM_TOURISTS=20
ROPEWAY_FORCE_EMERGENCY_AT_SEC=20
ROPEWAY_DURATION_US=60000000
```

**Weryfikacja:**
- Kolej zatrzymuje się w ~20 sekundzie
- Wymiana komunikatów między pracownikami
- Poprawne wznowienie

---

## Debugowanie

### Włączenie Debug Logów

W `include/core/Flags.h`:

```cpp
namespace Flags::Logging {
    constexpr bool IS_DEBUG_ENABLED = true;  // Zmień na true
}
```

Rekompiluj:
```bash
make clean && make
```

### Sprawdzanie Zasobów IPC

```bash
# Lista wszystkich zasobów IPC
ipcs

# Przykładowe wyjście:
------ Shared Memory Segments --------
key        shmid      owner      perms      bytes
0x53012345 65536      user       600        1234

------ Semaphore Arrays --------
key        semid      owner      perms      nsems
0x4d012345 32768      user       600        21

------ Message Queues --------
key        msqid      owner      perms      used-bytes
0x57012345 0          user       600        0
```

### Usuwanie "Wiszących" Zasobów

Jeśli program się zawiesi, zasoby mogą pozostać:

```bash
# Usuń konkretny zasób
ipcrm -m <shmid>   # Shared memory
ipcrm -s <semid>   # Semaphores
ipcrm -q <msqid>   # Message queues

# Lub usuń wszystkie (ostrożnie!)
ipcrm -a
```

### GDB Debugging

```bash
# Uruchom pod GDB
gdb ./ropeway_simulation

# Przydatne komendy
(gdb) break TouristProcess::buyTicket
(gdb) run
(gdb) info threads
(gdb) thread <id>
(gdb) backtrace
```

### Śledzenie Procesów

```bash
# Lista procesów symulacji
ps aux | grep ropeway

# Śledzenie syscalli
strace -p <pid>

# Śledzenie fork/exec
strace -f ./ropeway_simulation
```

---

## Raport Końcowy

Po zakończeniu symulacji generowany jest `daily_report.txt`:

```
ROPEWAY DAILY REPORT
====================
Operating hours: 08:00 - 18:00

FINANCIAL
  Revenue:        1234.50 PLN
  Tickets sold:   50

TOURISTS (50 total)
  Children (<10): 5
  Adults (10-64): 40
  Seniors (65+):  5
  VIP:            3

TYPES
  Pedestrians:    30
  Cyclists:       20

RIDES
  Total rides:    120
  Gate passages:  240

EMERGENCY
  Stops:          1

RIDES PER TOURIST/TICKET
Tourist    Ticket     Age   Type       VIP    Rides    Guardian
--------------------------------------------------------------
1          1          35    Pedestrian Yes    5        -1
2          2          7     Pedestrian No     3        1
...

GATE PASSAGE LOG
Time     Tourist    Ticket     Gate   Type     Allowed
--------------------------------------------------------------
08:00    1          1          0      Entry    Yes
08:01    1          1          0      Ride     Yes
...
```

---

## Częste Problemy

### 1. "Cannot open ropeway.env"

```bash
# Upewnij się że plik istnieje w katalogu z executablem
cp ../ropeway.env ./ropeway.env
```

### 2. "shmget: Permission denied"

```bash
# Usuń stare zasoby IPC
ipcrm -a

# Sprawdź czy nie ma innych instancji
killall ropeway_simulation tourist_process cashier_process
```

### 3. Procesy Zombie

```bash
# Znajdź zombie
ps aux | grep defunct

# Zabij rodzica
kill -9 <parent_pid>
```

### 4. "Message queue full"

To normalne na macOS (małe limity). Zmniejsz:

```bash
ROPEWAY_NUM_TOURISTS=20  # Mniej turystów
```

### 5. Deadlock

Sprawdź kolejność locków w kodzie. Użyj:

```bash
# Sprawdź stany procesów
ps -o pid,stat,comm | grep ropeway
# 'D' = czeka na I/O, 'S' = sleep, 'R' = running
```

---

## Wskazówki

1. **Zacznij od małej liczby turystów** (10-20)
2. **Włącz debug logi** przy szukaniu błędów
3. **Sprawdzaj raport** po każdym teście
4. **Używaj `ipcs`** regularnie do sprawdzania zasobów
5. **Czytaj logi od końca** - tam często jest przyczyna błędu
