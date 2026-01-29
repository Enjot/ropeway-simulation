# Dokumentacja Projektu - Symulacja Kolei Linowej

## Spis Treści

| # | Rozdział | Opis |
|---|----------|------|
| 1 | [Przegląd Projektu](01_OVERVIEW.md) | Wprowadzenie, architektura, struktura katalogów |
| 2 | [Mechanizmy IPC](02_IPC_MECHANISMS.md) | Pamięć dzielona, semafory, kolejki komunikatów |
| 3 | [Procesy](03_PROCESSES.md) | Opis każdego procesu w symulacji |
| 4 | [Cykl Życia Turysty](04_TOURIST_LIFECYCLE.md) | Stany i przepływ turysty przez system |
| 5 | [Protokół Awaryjny](05_EMERGENCY_PROTOCOL.md) | Zatrzymanie i wznowienie kolei |
| 6 | [Synchronizacja](06_SYNCHRONIZATION.md) | Wzorce współbieżności i unikanie błędów |
| 7 | [Uruchamianie i Testowanie](07_RUNNING_AND_TESTING.md) | Kompilacja, konfiguracja, debugowanie |
| 8 | [Słownik Pojęć](08_GLOSSARY.md) | Definicje terminów i skrótów |

---

## Szybki Start

### 1. Kompilacja

```bash
mkdir build && cd build
cmake ..
make
```

### 2. Konfiguracja

```bash
cp ../ropeway.env ./ropeway.env
# Edytuj parametry według potrzeb
```

### 3. Uruchomienie

```bash
./ropeway_simulation
```

---

## Kluczowe Koncepcje

### System V IPC
- **Shared Memory** - współdzielony stan między procesami
- **Semafory** - synchronizacja dostępu do zasobów
- **Message Queues** - komunikacja między procesami

### Procesy
- **Main** - orkiestrator symulacji
- **Logger** - centralne logowanie
- **Cashier** - sprzedaż biletów
- **Workers** - kontrola stacji (dolna/górna)
- **Tourists** - symulowani użytkownicy

### Synchronizacja
- Mutexy przez semafory binarne
- Kontrola pojemności przez semafory zliczające
- Priorytety przez typy wiadomości (mtype)

---

## Mapa Kodu

```
include/
├── core/
│   ├── Config.h          # Konfiguracja z env
│   ├── Simulation.h      # Główna klasa symulacji
│   ├── Constants.h       # Stałe wymagań
│   └── Flags.h           # Flagi kompilacji
├── ipc/
│   ├── IpcManager.h      # Zarządzanie zasobami IPC
│   └── core/
│       ├── Semaphore.h   # Wrapper semaforów
│       ├── SharedMemory.h # Wrapper pamięci dzielonej
│       └── MessageQueue.h # Wrapper kolejek
├── tourist/
│   ├── Tourist.h         # Struktura turysty
│   ├── TouristState.h    # Stany turysty
│   └── TouristType.h     # Typy (pieszy/rowerzysta)
├── ropeway/
│   ├── chair/            # Krzesełka i kolejka
│   ├── gate/             # Bramki wejścia/jazdy
│   └── worker/           # Komunikacja pracowników
├── entrance/
│   ├── Ticket.h          # Bilety
│   └── CashierMessage.h  # Komunikacja z kasą
├── logging/
│   └── Logger.h          # System logowania
└── utils/
    ├── ProcessSpawner.h  # Tworzenie procesów
    ├── SignalHelper.h    # Obsługa sygnałów
    └── TimeHelper.h      # Czas symulowany

src/
├── core/main.cpp         # Entry point
├── tourist/TouristProcess.cpp
├── entrance/CashierProcess.cpp
├── ropeway/LowerStationWorker.cpp
├── ropeway/UpperStationWorker.cpp
└── logging/LoggerProcess.cpp
```

---

## FAQ

### Dlaczego procesy zamiast wątków?
Wymaganie projektu - symulacja ma działać na procesach z użyciem `fork()` + `exec()`.

### Dlaczego System V IPC zamiast POSIX?
Wymaganie projektu - należy użyć System V IPC (shmget, semget, msgget).

### Jak działa czas symulowany?
1 sekunda rzeczywista = TIME_SCALE sekund symulowanych. Domyślnie 600x = 10 minut symulowanych na sekundę.

### Jak zatrzymać symulację?
- Ctrl+C - graceful shutdown
- Ctrl+Z - pauza (fg wznawia)

### Co zrobić gdy zasoby IPC "wiszą"?
```bash
ipcs        # Lista zasobów
ipcrm -a    # Usuń wszystkie
```

---

## Przydatne Komendy

```bash
# Kompilacja z debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Lista procesów symulacji
ps aux | grep ropeway

# Śledzenie syscalli
strace -f ./ropeway_simulation

# Zasoby IPC
ipcs

# Czyszczenie zasobów
ipcrm -a
```

---

## Licencja

Ten projekt jest udostępniony na potrzeby edukacyjne.
