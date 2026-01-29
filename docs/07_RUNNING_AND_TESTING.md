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
├── lib/
│   └── test_helpers.sh       # Funkcje pomocnicze dla testów
├── reports/                  # Raporty z testów (generowane)
│   ├── test1_station_capacity_limit.txt
│   ├── test1_simulation.log
│   ├── test2_children_guardian.txt
│   ├── test2_simulation.log
│   ├── ...
│   └── test_summary.txt
├── run_all_tests.sh          # Główny runner testów
├── test_station_capacity.sh  # Test 1
├── test_children_guardian.sh # Test 2
├── test_vip_priority.sh      # Test 3
└── test_emergency_stop.sh    # Test 4
```

### Uruchamianie Testów

```bash
# Przez make (z katalogu build)
make test

# Lub bezpośrednio
cd tests
./run_all_tests.sh

# Z opcjami
./run_all_tests.sh -b ../build    # Określ katalog build
./run_all_tests.sh -v             # Verbose output
./run_all_tests.sh -t 2           # Uruchom tylko test 2

# Pojedynczy test bezpośrednio
./test_station_capacity.sh ../build
```

### Funkcje Pomocnicze (test_helpers.sh)

| Funkcja | Opis |
|---------|------|
| `setup_test` | Inicjalizuje środowisko testu, czyści stare pliki |
| `run_simulation` | Uruchamia symulację z timeoutem |
| `cleanup_ipc` | Usuwa zasoby IPC (shm, sem, msg) |
| `cleanup_processes` | Zabija pozostałe procesy symulacji |
| `setup_cleanup_trap` | Ustawia trap na SIGINT/SIGTERM |
| `check_log_pattern` | Sprawdza czy wzorzec istnieje w logach |
| `count_log_pattern` | Zlicza wystąpienia wzorca |
| `report_header` | Tworzy nagłówek raportu |
| `report_check` | Dodaje wynik testu (PASS/FAIL) |
| `report_finish` | Finalizuje raport z końcowym wynikiem |
| `strip_ansi` | Usuwa kody ANSI z tekstu |

### Test 1: Limit Pojemności Stacji

**Cel:** Sprawdzić czy limit N osób na stacji nie jest przekraczany.

**Konfiguracja:**
```bash
ROPEWAY_NUM_TOURISTS=15
ROPEWAY_STATION_CAPACITY=5
ROPEWAY_DURATION_US=30000000  # 30 sekund
```

**Weryfikacja:**
- Symulacja zakończyła się poprawnie
- Brak procesów zombie
- Liczba osób na stacji nigdy nie przekroczyła N=5
- Turyści byli kolejkowani gdy stacja pełna

### Test 2: Dzieci z Opiekunem

**Cel:** Sprawdzić czy dzieci <8 lat jadą tylko z dorosłym (max 2 dzieci na dorosłego).

**Konfiguracja:**
```bash
ROPEWAY_NUM_TOURISTS=10
ROPEWAY_STATION_CAPACITY=10
ROPEWAY_CHILD_CHANCE_PCT=80
ROPEWAY_DURATION_US=30000000
```

**Weryfikacja:**
- Dzieci zostały wygenerowane (widoczne w daily_report)
- Wątki dzieci utworzone z rodzicami
- Każde dziecko ma przypisanego opiekuna

### Test 3: Priorytet VIP

**Cel:** Sprawdzić czy VIP-y otrzymują priorytetowe wejście.

**Konfiguracja:**
```bash
ROPEWAY_NUM_TOURISTS=20
ROPEWAY_VIP_CHANCE_PCT=30
ROPEWAY_STATION_CAPACITY=5
ROPEWAY_DURATION_US=30000000
```

**Weryfikacja:**
- VIP-y zostały wygenerowane
- VIP-y wchodzą przed zwykłymi turystami w kolejce
- Brak zagłodzenia zwykłych turystów

### Test 4: Zatrzymanie Awaryjne

**Cel:** Sprawdzić protokół emergency stop i wznowienia.

**Konfiguracja:**
```bash
ROPEWAY_NUM_TOURISTS=10
ROPEWAY_FORCE_EMERGENCY_AT_SEC=5
ROPEWAY_DURATION_US=30000000
```

**Weryfikacja:**
- Emergency stop został wywołany
- Wznowienie operacji nastąpiło poprawnie
- Turyści wznowili przejazdy po emergency

### Raporty z Testów

Po uruchomieniu testów generowane są raporty w `tests/reports/`:

```
================================================================================
TEST REPORT: Station Capacity Limit (N=5)
Date: 2026-01-29 17:19:56
================================================================================

CONFIGURATION:
  - NUM_TOURISTS: 15
  - STATION_CAPACITY: 5
  - DURATION: 30s

RESULTS:
  [PASS] Simulation completed (exit code: 0)
  [PASS] No zombie processes found
  [PASS] Station capacity never exceeded (max observed: 5)
  [PASS] Tourists were queued (12 waited before entry)

STATISTICS:
  - Total tourists processed: 15
  - Max concurrent in station: 5
  - Tourists queued (waited): 12
  - Total rides completed: 45

VERIFICATION LOG EXCERPTS:
  [08:00] [INFO ] [LowerWorker] Entry granted to Tourist 1
  [08:00] [INFO ] [LowerWorker] Entry granted to Tourist 2
  ...

RESULT: PASS
================================================================================
```

### Cleanup przy SIGINT

Testy ustawiają trap na SIGINT (Ctrl+C) i SIGTERM. Przy przerwaniu:
1. Wyświetla komunikat "Interrupted - cleaning up..."
2. Zabija pozostałe procesy symulacji
3. Usuwa zasoby IPC (shared memory, semafory, kolejki)
4. Kończy z kodem 130

```bash
# Przerwanie testu
^C
Interrupted - cleaning up...

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
