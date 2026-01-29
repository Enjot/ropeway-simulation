# Procesy w Symulacji

## Tworzenie Procesów

Każdy proces jest tworzony przez główny proces symulacji za pomocą `fork()` + `exec()`:

```cpp
pid_t pid = fork();
if (pid == 0) {
    // Proces potomny
    execv(processPath, argv);
    _exit(1);  // Tylko jeśli exec się nie powiedzie
}
// Proces rodzica kontynuuje
```

### ProcessSpawner

Projekt wykorzystuje helper `ProcessSpawner`:

```cpp
// Prosty spawn
pid_t pid = ProcessSpawner::spawn("tourist_process", {
    std::to_string(id),
    std::to_string(age),
    // ... argumenty
});

// Spawn z kluczami IPC
pid_t pid = ProcessSpawner::spawnWithKeys("cashier_process",
    shmKey, semKey, cashierMsgKey, logMsgKey);
```

---

## 1. Główny Proces (Simulation)

**Plik:** `src/core/main.cpp`, `include/core/Simulation.h`

### Odpowiedzialności

1. Ładowanie konfiguracji z `ropeway.env`
2. Tworzenie zasobów IPC (IpcManager)
3. Spawn procesów roboczych
4. Monitorowanie czasu symulacji
5. Obsługa zamknięcia (Tk)
6. Generowanie raportu końcowego
7. Czyszczenie zasobów

### Główna Pętla

```cpp
void Simulation::mainLoop() {
    spawnTourists();

    while (!signals_.exit) {
        // Oblicz czas symulowany
        uint32_t simSeconds = TimeHelper::getSimulatedSeconds(startTime_, paused);
        uint32_t simHour = simSeconds / 3600;

        // Sprawdź czas zamknięcia
        if (simHour >= Config::Simulation::CLOSING_HOUR()) {
            // Ustaw stan CLOSING
            // Powiadom kasę
        }

        // Czekaj na opróżnienie stacji
        if (touristsInStation == 0 && chairsInUse == 0) {
            // Zakończ symulację
            break;
        }

        usleep(Config::Time::MAIN_LOOP_POLL_US());
    }
}
```

### Kolejność Zamykania

```
1. Wyślij SIGTERM do Cashier, Workers, Tourists
2. Czekaj na zakończenie (waitFor)
3. Wyślij SIGTERM do Logger
4. Czekaj na Logger
5. Zwolnij zasoby IPC
```

---

## 2. Logger Process

**Plik:** `src/logging/LoggerProcess.cpp`

### Cel

Centralizuje logowanie z wszystkich procesów. Zapewnia:
- Uporządkowane wyświetlanie (według numeru sekwencyjnego)
- Kolorowe oznaczenie źródeł
- Czas symulowany w logach

### Architektura

```
┌──────────┐  ┌──────────┐  ┌──────────┐
│ Tourist  │  │ Cashier  │  │ Worker   │
└────┬─────┘  └────┬─────┘  └────┬─────┘
     │             │             │
     └─────────────┼─────────────┘
                   │
            ┌──────┴──────┐
            │ Log Queue   │
            │ (messages)  │
            └──────┬──────┘
                   │
            ┌──────┴──────┐
            │   Logger    │
            │   Process   │
            └──────┬──────┘
                   │
                stdout
```

### Obsługa Wiadomości

```cpp
void LoggerProcess::run() {
    while (!signals_.exit) {
        auto msg = logQueue_.receiveInterruptible(0);  // Odbierz dowolny typ
        if (msg.has_value()) {
            printMessage(*msg);
        }
    }

    // Drain kolejki przed wyjściem
    while (auto msg = logQueue_.tryReceive(0)) {
        printMessage(*msg);
    }
}
```

### Kolory Źródeł

| Źródło | Kolor |
|--------|-------|
| LowerWorker | Cyan |
| UpperWorker | Magenta |
| Cashier | Yellow |
| Tourist | Green |
| Other | White |
| ERROR | Red |

---

## 3. Cashier Process

**Plik:** `src/entrance/CashierProcess.cpp`

### Cel

Obsługuje sprzedaż biletów. Dla każdego turysty:
1. Odbiera żądanie zakupu
2. Oblicza cenę z ewentualnymi zniżkami
3. Przyznaje status VIP
4. Wysyła odpowiedź z biletem

### Pętla Główna

```cpp
void CashierProcess::run() {
    sem_.post(Semaphore::Index::CASHIER_READY, 1, true);  // Sygnalizuj gotowość

    while (!signals_.exit) {
        auto request = cashierQueue_.receiveInterruptible(CashierMsgType::REQUEST);
        if (!request.has_value()) continue;

        // Sprawdź sentinel zamknięcia
        if (request->touristId == CASHIER_CLOSING_SENTINEL) {
            isClosing_ = true;
            continue;
        }

        // Wygeneruj bilet
        TicketResponse response = generateTicket(*request);

        // Wyślij odpowiedź na osobny typ (touristId + RESPONSE_BASE)
        responseQueue_.send(response,
            request->touristId + CashierMsgType::RESPONSE_BASE);
    }
}
```

### Obliczanie Zniżek

```cpp
double calculateDiscount(uint32_t age) {
    if (age < Constants::Discount::CHILD_DISCOUNT_AGE) {
        return Constants::Discount::CHILD_DISCOUNT;  // 25%
    }
    if (age >= Constants::Age::SENIOR_AGE_FROM) {
        return Constants::Discount::SENIOR_DISCOUNT;  // 25%
    }
    return 0.0;
}
```

---

## 4. Lower Station Worker

**Plik:** `src/ropeway/LowerStationWorker.cpp`

### Odpowiedzialności

1. Obsługa bramek wejściowych (4 bramki)
2. Zarządzanie kolejką oczekujących
3. Przydzielanie krzesełek
4. Wykrywanie zagrożeń (losowe)
5. Inicjowanie protokołu awaryjnego

### Obsługa Bramek

```cpp
void LowerStationWorker::handleEntryRequests() {
    // Odbierz z priorytetem (VIP pierwszy)
    auto request = entryQueue_.receive(EntryGateMsgType::PRIORITY_RECEIVE);

    if (request.has_value()) {
        bool canEnter = checkCapacity();  // Czy jest miejsce na stacji?

        EntryGateResponse response;
        response.touristId = request->touristId;
        response.allowed = canEnter;

        if (canEnter) {
            incrementStationCount();
            logGatePassage(request->touristId, GateType::ENTRY, true);
        }

        // Odpowiedz na unikalny typ dla tego turysty
        responseQueue_.send(response,
            request->touristId + EntryGateMsgType::RESPONSE_BASE);
    }
}
```

### Przydzielanie Krzesełek

```cpp
void LowerStationWorker::assignChairs() {
    Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_CHAIRS);

    auto& queue = state_->chairPool.boardingQueue;

    for (uint32_t i = 0; i < queue.count; ) {
        auto& tourist = queue.entries[i];

        // Znajdź wolne krzesełko
        int chairId = findAvailableChair(tourist.slots);

        if (chairId >= 0) {
            // Przypisz i powiadom turystę
            tourist.assignedChairId = chairId;
            tourist.readyToBoard = true;
            // Tourist wykryje to przez polling pamięci dzielonej
        }

        ++i;
    }
}
```

### Wykrywanie Zagrożeń

```cpp
void LowerStationWorker::checkForDanger() {
    // Losowe "wykrycie zagrożenia" (1% szansy co iterację)
    if (randomChance() < 0.01) {
        initiateEmergencyStop();
    }
}

void LowerStationWorker::initiateEmergencyStop() {
    // Ustaw stan
    state_->operational.state = RopewayState::EMERGENCY_STOP;
    state_->operational.dangerDetectorPid = getpid();

    // Wyślij SIGUSR1 do wszystkich turystów
    sendSignalToTourists(SIGUSR1);

    // Wyślij wiadomość do górnego pracownika
    WorkerMessage msg;
    msg.signal = WorkerSignal::DANGER_DETECTED;
    workerQueue_.send(msg, UPPER_WORKER_ID);
}
```

---

## 5. Upper Station Worker

**Plik:** `src/ropeway/UpperStationWorker.cpp`

### Odpowiedzialności

1. Monitorowanie przyjeżdżających krzesełek
2. Zarządzanie drogami wyjściowymi
3. Odpowiadanie na protokół awaryjny

### Obsługa Przyjazdu

```cpp
void UpperStationWorker::handleArrivals() {
    Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_CHAIRS);

    time_t now = time(nullptr);

    for (auto& chair : state_->chairPool.chairs) {
        if (chair.isOccupied && now >= chair.arrivalTime) {
            // Krzesełko dotarło
            processArrival(chair);
            releaseChair(chair);
        }
    }
}
```

### Protokół Awaryjny (Odpowiedź)

```cpp
void UpperStationWorker::handleWorkerMessages() {
    auto msg = workerQueue_.tryReceive(MY_WORKER_ID);

    if (msg.has_value()) {
        if (msg->signal == WorkerSignal::DANGER_DETECTED) {
            // Potwierdź gotowość do wznowienia
            WorkerMessage response;
            response.signal = WorkerSignal::READY_TO_START;
            workerQueue_.send(response, LOWER_WORKER_ID);
        }
    }
}
```

---

## 6. Tourist Process

**Plik:** `src/tourist/TouristProcess.cpp`

### Cel

Symuluje pojedynczego turystę przechodzącego przez system. Każdy turysta to osobny proces z własnym PID.

### Cykl Życia

```
BUYING_TICKET → WAITING_ENTRY → WAITING_BOARDING → ON_CHAIR
                                                       ↓
                                                   AT_TOP
                                                       ↓
                                        (cyclist) ON_TRAIL → FINISHED
                                        (pedestrian) → FINISHED
```

### Obsługa Dzieci (Wątki)

Dzieci są obsługiwane jako **wątki** wewnątrz procesu turysty-rodzica:

```cpp
void TouristProcess::spawnChildThreads() {
    for (uint32_t i = 0; i < tourist_.childCount; ++i) {
        childThreads_.emplace_back([this, i]() {
            // Dziecko "jedzie" razem z rodzicem
            // Logika synchronizacji przez rodzica
        });
    }
}
```

### Reakcja na Sygnały

```cpp
void TouristProcess::handleSignals() {
    if (signals_.emergency) {
        // Zatrzymaj się i czekaj
        while (!signals_.resume && !signals_.exit) {
            usleep(100000);  // 100ms
        }
        signals_.emergency = 0;
        signals_.resume = 0;
    }
}
```

---

## Diagram Interakcji

```
                    ┌─────────────┐
                    │   Main      │
                    │  Process    │
                    └──────┬──────┘
                           │ fork+exec
         ┌─────────────────┼─────────────────┐
         │                 │                 │
         ▼                 ▼                 ▼
    ┌─────────┐      ┌─────────┐      ┌─────────┐
    │ Logger  │      │ Cashier │      │ Workers │
    └─────────┘      └────┬────┘      └────┬────┘
         ▲                │                │
         │                │                │
    LogMessage     TicketReq/Resp    EntryGateReq/Resp
         │                │                │
         │                ▼                ▼
         │           ┌─────────────────────────┐
         └───────────│     TOURISTS (1..N)     │
                     └─────────────────────────┘
```

---

## Obsługa Sygnałów

Każdy proces rejestruje handlery dla:

| Sygnał | Znaczenie | Reakcja |
|--------|-----------|---------|
| SIGTERM | Zamknięcie | Ustaw `exit` flag, zakończ |
| SIGINT | Ctrl+C | Jak SIGTERM |
| SIGUSR1 | Zatrzymanie awaryjne | Ustaw `emergency` flag |
| SIGUSR2 | Wznowienie | Ustaw `resume` flag |

```cpp
SignalHelper::setup(signals_, SignalHelper::Mode::WORKER);
// lub
SignalHelper::setup(signals_, SignalHelper::Mode::TOURIST);
```

---

Następny rozdział: [Cykl życia turysty](04_TOURIST_LIFECYCLE.md)
