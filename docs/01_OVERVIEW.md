# Przegląd Projektu - Symulacja Kolei Linowej

## Wprowadzenie

Ten projekt implementuje symulację kolei linowej (krzesełkowej) działającej w górskiej miejscowości. Symulacja wykorzystuje **procesy** (nie wątki) do modelowania różnych aktorów systemu oraz mechanizmy **System V IPC** do komunikacji i synchronizacji.

## Cel Projektu

Projekt demonstruje praktyczne zastosowanie:
- Programowania wieloprocesowego (`fork()` + `exec()`)
- Mechanizmów IPC System V (pamięć dzielona, semafory, kolejki komunikatów)
- Obsługi sygnałów POSIX
- Synchronizacji procesów bez użycia muteksów/wątków

## Architektura Systemu

```
┌─────────────────────────────────────────────────────────────────────┐
│                        GŁÓWNY PROCES (main)                         │
│                                                                     │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌────────────┐  │
│  │   Logger    │  │   Cashier   │  │ LowerWorker │  │UpperWorker │  │
│  │   Process   │  │   Process   │  │   Process   │  │  Process   │  │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └──────┬─────┘  │
│         │                │                │                │        │
│  ┌──────┴────────────────┴────────────────┴────────────────┴──────┐ │
│  │                     SHARED MEMORY (IPC)                        │ │
│  │  ┌─────────────────┬─────────────────┬─────────────────┐       │ │
│  │  │  Operational    │   Chair Pool    │   Statistics    │       │ │
│  │  │     State       │     State       │     State       │       │ │
│  │  └─────────────────┴─────────────────┴─────────────────┘       │ │
│  └────────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │                    TOURIST PROCESSES (1..N)                    │ │
│  │  Tourist 1  │  Tourist 2  │  Tourist 3  │  ...  │  Tourist N   │ │
│  └────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

## Procesy w Symulacji

### 1. Proces Główny (Simulation)
- Tworzy zasoby IPC
- Spawnuje pozostałe procesy
- Monitoruje czas symulacji
- Generuje raport końcowy
- Czyści zasoby po zakończeniu

### 2. Logger Process
- Odbiera komunikaty logów z kolejki
- Zapewnia uporządkowane wyświetlanie logów
- Kolorowe oznaczenie źródeł

### 3. Cashier Process
- Obsługuje sprzedaż biletów
- Oblicza zniżki (dzieci, seniorzy)
- Przyznaje status VIP

### 4. Lower Station Worker
- Kontroluje stację dolną
- Zarządza kolejką do wejścia
- Przydziela krzesełka
- Może zainicjować zatrzymanie awaryjne

### 5. Upper Station Worker
- Kontroluje stację górną
- Monitoruje przyjazdy krzesełek
- Zarządza wyjściami (trasy, ścieżki)

### 6. Tourist Processes (N procesów)
- Każdy turysta to osobny proces
- Dzieci i rowery obsługiwane jako wątki wewnątrz procesu turysty
- Pełny cykl życia: bilet → bramka → krzesełko → góra → wyjście

## Przepływ Danych

```
Tourist ──[TicketRequest]──> Cashier
         <──[TicketResponse]──

Tourist ──[EntryGateRequest]──> LowerWorker
         <──[EntryGateResponse]──

LowerWorker ←──[WorkerMessage]──→ UpperWorker
            (emergency/resume protocol)

All Processes ──[LogMessage]──> Logger
```

## Synchronizacja

### Semafory (21 w zestawie)
- **STATION_CAPACITY** - limit N osób na stacji
- **CHAIRS_AVAILABLE** - dostępne krzesełka (max 36)
- **SHM_OPERATIONAL** - mutex na stan operacyjny
- **SHM_CHAIRS** - mutex na pulę krzesełek
- **SHM_STATS** - mutex na statystyki

### Kolejka priorytetowa (VIP)
- Turyści VIP używają niższego mtype → odbierani pierwsi
- `msgrcv()` z ujemnym typem zwraca wiadomość o najniższym typie

## Czas Symulowany

Symulacja wykorzystuje **przeskalowany czas**:
- 1 sekunda rzeczywista = TIME_SCALE sekund symulowanych
- Domyślnie: TIME_SCALE = 600 (10 minut = 1 sekunda)
- Godziny otwarcia: 8:00 - 18:00 (symulowane)

## Pliki Konfiguracyjne

### ropeway.env
```bash
ROPEWAY_NUM_TOURISTS=50
ROPEWAY_STATION_CAPACITY=20
ROPEWAY_TIME_SCALE=600
ROPEWAY_OPENING_HOUR=8
ROPEWAY_CLOSING_HOUR=18
# ... więcej opcji
```

## Wymagania Systemowe

- System POSIX (Linux)
- C++17
- CMake 3.16+
- System V IPC (shmget, semget, msgget)

## Struktura Katalogów

```
ropeway-simulation/
├── include/           # Pliki nagłówkowe (.h)
│   ├── core/          # Główne komponenty
│   ├── ipc/           # Wrapery IPC
│   ├── tourist/       # Struktury turysty
│   ├── ropeway/       # Krzesełka, bramki, pracownicy
│   ├── entrance/      # Kasa, bilety
│   ├── logging/       # System logowania
│   ├── stats/         # Statystyki
│   └── utils/         # Narzędzia pomocnicze
├── src/               # Implementacje (.cpp)
├── docs/              # Dokumentacja
├── tests/             # Testy
├── ropeway.env        # Konfiguracja
└── CMakeLists.txt     # Build system
```

## Następne Kroki

1. [Mechanizmy IPC](02_IPC_MECHANISMS.md) - szczegóły komunikacji
2. [Procesy](03_PROCESSES.md) - opis każdego procesu
3. [Cykl życia turysty](04_TOURIST_LIFECYCLE.md) - przepływ turysty
4. [Protokół awaryjny](05_EMERGENCY_PROTOCOL.md) - zatrzymanie/wznowienie
5. [Synchronizacja](06_SYNCHRONIZATION.md) - semafory i locki
