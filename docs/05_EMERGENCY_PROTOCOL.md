# Protokół Awaryjny

## Przegląd

Protokół awaryjny umożliwia natychmiastowe zatrzymanie kolei linowej w przypadku wykrycia zagrożenia. Wymaga koordynacji między pracownikami obu stacji.

## Sygnały Używane

| Sygnał | Cel | Wysyłany przez |
|--------|-----|----------------|
| SIGUSR1 | Emergency Stop | Worker → Wszystkie procesy |
| SIGUSR2 | Resume | Worker → Wszystkie procesy |

---

## Sekwencja Zatrzymania

```
┌─────────────────────────────────────────────────────────────────────────┐
│                                                                         │
│   1. WYKRYCIE ZAGROŻENIA (LowerStationWorker)                           │
│   ─────────────────────────────────────────────                         │
│                                                                         │
│   ┌─────────────────┐                                                   │
│   │ LowerWorker     │ ── Wykrywa "zagrożenie" (losowe lub wymuszone)    │
│   │ detects danger  │                                                   │
│   └────────┬────────┘                                                   │
│            │                                                            │
│            ▼                                                            │
│   2. USTAWIENIE STANU EMERGENCY                                         │
│   ───────────────────────────────                                       │
│                                                                         │
│   ┌─────────────────┐                                                   │
│   │ Set state =     │                                                   │
│   │ EMERGENCY_STOP  │ ── state_->operational.state = EMERGENCY_STOP     │
│   │ in SharedMemory │    state_->operational.dangerDetectorPid = pid    │
│   └────────┬────────┘                                                   │
│            │                                                            │
│            ▼                                                            │
│   3. WYSŁANIE SIGUSR1 DO TURYSTÓW                                       │
│   ─────────────────────────────────                                     │
│                                                                         │
│   ┌─────────────────┐     SIGUSR1      ┌─────────────────┐              │
│   │ LowerWorker     │ ───────────────> │ All Tourists    │              │
│   │ sends signal    │                  │ set emergency=1 │              │
│   └────────┬────────┘                  └─────────────────┘              │
│            │                                                            │
│            ▼                                                            │
│   4. POWIADOMIENIE GÓRNEGO PRACOWNIKA                                   │
│   ───────────────────────────────────                                   │
│                                                                         │
│   ┌─────────────────┐  WorkerMessage   ┌─────────────────┐              │
│   │ LowerWorker     │ ───────────────> │ UpperWorker     │              │
│   │ DANGER_DETECTED │                  │ receives msg    │              │
│   └─────────────────┘                  └─────────────────┘              │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Sekwencja Wznowienia

```
┌─────────────────────────────────────────────────────────────────────────┐
│                                                                         │
│   5. GÓRNY PRACOWNIK POTWIERDZA GOTOWOŚĆ                                │
│   ────────────────────────────────────────                              │
│                                                                         │
│   ┌─────────────────┐  WorkerMessage   ┌─────────────────┐              │
│   │ UpperWorker     │ ───────────────> │ LowerWorker     │              │
│   │ READY_TO_START  │                  │ receives conf.  │              │
│   └─────────────────┘                  └────────┬────────┘              │
│                                                 │                       │
│            (po pewnym czasie - symulacja naprawy)                       │
│                                                 │                       │
│                                                 ▼                       │
│   6. WZNOWIENIE PRZEZ DOLNEGO PRACOWNIKA                                │
│   ──────────────────────────────────────                                │
│                                                                         │
│   ┌─────────────────┐                                                   │
│   │ Set state =     │                                                   │
│   │ RUNNING         │ ── Only if dangerDetectorPid == my pid            │
│   │ (or CLOSING)    │                                                   │
│   └────────┬────────┘                                                   │
│            │                                                            │
│            ▼                                                            │
│   7. WYSŁANIE SIGUSR2 DO TURYSTÓW                                       │
│   ─────────────────────────────────                                     │
│                                                                         │
│   ┌─────────────────┐     SIGUSR2      ┌─────────────────┐              │
│   │ LowerWorker     │ ───────────────> │ All Tourists    │              │
│   │ sends signal    │                  │ set resume=1    │              │
│   └─────────────────┘                  └─────────────────┘              │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Kod - Inicjowanie Zatrzymania

```cpp
// LowerStationWorker.cpp
void LowerStationWorker::initiateEmergencyStop() {
    Logger::warn(Logger::Source::LowerWorker, tag_,
        "!!! DANGER DETECTED - EMERGENCY STOP !!!");

    // 1. Ustaw stan w pamięci dzielonej
    {
        Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
        state_->operational.state = RopewayState::EMERGENCY_STOP;
        state_->operational.dangerDetectorPid = getpid();
    }

    // 2. Zarejestruj w statystykach
    {
        Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_STATS);
        emergencyRecordIndex_ = state_->stats.dailyStats.recordEmergencyStart(1);
    }

    // 3. Wyślij wiadomość do górnego pracownika
    WorkerMessage msg;
    msg.senderId = 1;  // LowerWorker ID
    msg.receiverId = 2;  // UpperWorker ID
    msg.signal = WorkerSignal::DANGER_DETECTED;
    msg.timestamp = time(nullptr);
    workerQueue_.send(msg, 2);  // mtype = receiver ID

    // 4. Wyślij SIGUSR1 do wszystkich turystów
    sendEmergencySignalToTourists(SIGUSR1);
}
```

---

## Kod - Odpowiedź Górnego Pracownika

```cpp
// UpperStationWorker.cpp
void UpperStationWorker::handleDangerNotification() {
    auto msg = workerQueue_.tryReceive(2);  // mtype = my ID

    if (msg.has_value() && msg->signal == WorkerSignal::DANGER_DETECTED) {
        Logger::warn(Logger::Source::UpperWorker, tag_,
            "Received DANGER notification from Lower Worker");

        // Zatrzymaj wszelkie operacje na górze
        suspendOperations();

        // Wyślij potwierdzenie gotowości
        WorkerMessage response;
        response.senderId = 2;
        response.receiverId = 1;
        response.signal = WorkerSignal::READY_TO_START;
        response.timestamp = time(nullptr);
        workerQueue_.send(response, 1);

        Logger::info(Logger::Source::UpperWorker, tag_,
            "Sent READY_TO_START to Lower Worker");
    }
}
```

---

## Kod - Wznowienie

```cpp
// LowerStationWorker.cpp
void LowerStationWorker::initiateResume() {
    // Sprawdź czy to my zainicjowaliśmy stop
    {
        Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
        if (state_->operational.dangerDetectorPid != getpid()) {
            return;  // Nie my - nie możemy wznowić
        }
    }

    // Czekaj na potwierdzenie od górnego pracownika
    while (!signals_.exit) {
        auto msg = workerQueue_.tryReceive(1);  // mtype = my ID

        if (msg.has_value() && msg->signal == WorkerSignal::READY_TO_START) {
            Logger::info(Logger::Source::LowerWorker, tag_,
                "Received READY confirmation from Upper Worker");
            break;
        }

        usleep(100000);  // 100ms
    }

    // Symulacja "naprawy" - poczekaj chwilę
    usleep(500000);  // 500ms

    // Wznów działanie
    {
        Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);

        // Sprawdź czy nadal jesteśmy w EMERGENCY_STOP
        if (state_->operational.state == RopewayState::EMERGENCY_STOP) {
            // Jeśli czas zamknięcia minął, przejdź do CLOSING
            if (state_->operational.acceptingNewTourists == false) {
                state_->operational.state = RopewayState::CLOSING;
            } else {
                state_->operational.state = RopewayState::RUNNING;
            }
            state_->operational.dangerDetectorPid = 0;
        }
    }

    // Zarejestruj koniec emergency
    {
        Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_STATS);
        state_->stats.dailyStats.recordEmergencyEnd(emergencyRecordIndex_);
    }

    // Wyślij SIGUSR2 do turystów
    sendEmergencySignalToTourists(SIGUSR2);

    Logger::info(Logger::Source::LowerWorker, tag_,
        ">>> ROPEWAY RESUMED <<<");
}
```

---

## Obsługa Sygnałów w Turystach

```cpp
// SignalHelper.h
namespace SignalHelper {
    namespace detail {
        inline void handler(const int32_t sig) {
            if (!g_flags) return;

            switch (sig) {
                case SIGUSR1:
                    g_flags->emergency = 1;
                    break;
                case SIGUSR2:
                    g_flags->resume = 1;
                    break;
                case SIGTERM:
                case SIGINT:
                    g_flags->exit = 1;
                    break;
            }
        }
    }
}

// TouristProcess.cpp
void TouristProcess::handleSignals() {
    if (signals_.emergency) {
        Logger::warn(Logger::Source::Tourist, tag_,
            "Emergency stop - pausing operations");

        // Czekaj na resume lub exit
        while (!signals_.resume && !signals_.exit) {
            usleep(100000);
        }

        if (signals_.resume) {
            Logger::info(Logger::Source::Tourist, tag_,
                "Received resume signal - continuing");
            signals_.emergency = 0;
            signals_.resume = 0;
        }
    }
}
```

---

## Diagram Czasowy

```
Czas ──────────────────────────────────────────────────────────────────>

LowerWorker:  [Wykrycie]──[EMERGENCY_STOP]──[Wysyłanie]──...──[Wznowienie]
                   │              │               │                │
                   │              │               │                │
                   ▼              │               ▼                ▼
SharedMemory: ────[state=RUNNING]─[state=EMERGENCY]────────[state=RUNNING]───
                                  │               │                │
                                  │               │                │
UpperWorker:  ────────────────────[Otrzymanie]───[READY]───────────────────
                                       │            │
                                       │            │
Tourists:     ─────────────────────[SIGUSR1]─[Czekają]────[SIGUSR2]─[Wznowienie]
                                  (zatrzymani)           (kontynuacja)
```

---

## Ważne Uwagi

### 1. Async-Signal-Safety

Handlery sygnałów muszą używać tylko funkcji async-signal-safe:
- `write()` - OK
- `printf()` - NIE!
- `malloc()` - NIE!
- Ustawianie `volatile sig_atomic_t` - OK

```cpp
// POPRAWNIE:
void handler(int sig) {
    g_flags->emergency = 1;  // volatile sig_atomic_t
}

// NIEPOPRAWNIE:
void handler(int sig) {
    printf("Emergency!\n");  // NIE! printf nie jest signal-safe
    Logger::info(...);       // NIE!
}
```

### 2. Tylko Inicjator Może Wznowić

Tylko pracownik który wykrył zagrożenie może wznowić działanie:

```cpp
if (state_->operational.dangerDetectorPid != getpid()) {
    return;  // Nie my - nie możemy wznowić
}
```

### 3. Obsługa Czasu Zamknięcia

Jeśli czas zamknięcia (Tk) nadejdzie podczas emergency stop:

```cpp
// W głównym procesie
if (closingTime && state == EMERGENCY_STOP) {
    // Nie zmieniaj na CLOSING - LowerWorker to zrobi po resume
    Logger::debug(..., "Closing time during emergency - deferring");
}

// W LowerWorker przy resume
if (!acceptingNewTourists) {
    state = RopewayState::CLOSING;  // Zamiast RUNNING
} else {
    state = RopewayState::RUNNING;
}
```

### 4. Test Wymuszony

Dla testów można wymusić emergency w określonym momencie:

```bash
# W ropeway.env
ROPEWAY_FORCE_EMERGENCY_AT_SEC=20  # Wymusi emergency po 20 sekundach
```

---

## Stany Systemu

```
                    ┌──────────────┐
                    │   STOPPED    │
                    │  (początek)  │
                    └──────┬───────┘
                           │ initState()
                           ▼
                    ┌──────────────┐
           ┌───────>│   RUNNING    │<────────┐
           │        └──────┬───────┘         │
           │               │                 │
           │  initiateResume()       Danger detected
           │               │                 │
           │               ▼                 │
           │        ┌──────────────┐         │
           └────────│  EMERGENCY   │─────────┘
                    │    STOP      │
                    └──────┬───────┘
                           │ (lub)
                           │ closingTime reached
                           ▼
                    ┌─────────────────────────┐
                    │   CLOSING               │
                    │ (nie przyjmuje nowych)  │
                    └──────┬──────────────────┘
                           │ station empty
                           ▼
                    ┌──────────────┐
                    │   STOPPED    │
                    │   (koniec)   │
                    └──────────────┘
```

---

Następny rozdział: [Synchronizacja](06_SYNCHRONIZATION.md)
