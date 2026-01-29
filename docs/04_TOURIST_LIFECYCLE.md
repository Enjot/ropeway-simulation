# Cykl Życia Turysty

## Przegląd

Każdy turysta przechodzi przez określone stany od przybycia do opuszczenia kolei linowej. Ten dokument opisuje szczegółowo każdy etap.

## Diagram Stanów

```
┌───────────────────────────────────────────────────────────────────────┐
│                                                                       │
│   ┌──────────────┐    ┌──────────────┐    ┌────────────────────┐      │
│   │   BUYING     │───>│   WAITING    │───>│     WAITING        │      │
│   │   TICKET     │    │    ENTRY     │    │     BOARDING       │      │
│   └──────────────┘    └──────────────┘    └─────────┬──────────┘      │
│          │                   │                      │                 │
│          │ (nie chce         │ (brak                │                 │
│          │  jeździć)         │  miejsca)            │                 │
│          │                   │                      ▼                 │
│          │                   │              ┌──────────────┐          │
│          │                   │              │   ON_CHAIR   │          │
│          │                   │              └───────┬──────┘          │
│          │                   │                      │                 │
│          │                   │                      ▼                 │
│          │                   │              ┌──────────────┐          │
│          │                   │              │    AT_TOP    │          │
│          │                   │              └───────┬──────┘          │
│          │                   │                      │                 │
│          │                   │         ┌────────────┴────────────┐    │
│          │                   │         │                         │    │
│          │                   │         ▼                         ▼    │
│          │                   │  ┌──────────────┐         (pedestrian) │
│          │                   │  │   ON_TRAIL   │              │       │
│          │                   │  │  (cyclist)   │              │       │
│          │                   │  └───────┬──────┘              │       │
│          │                   │          │                     │       │
│          │                   │          ▼                     │       │
│          ▼                   ▼          ▼                     ▼       │
│   ┌──────────────────────────────────────────────────────────────┐    │
│   │                         FINISHED                             │    │
│   └──────────────────────────────────────────────────────────────┘    │
│                                                                       │
└───────────────────────────────────────────────────────────────────────┘
```

---

## Stan 1: BUYING_TICKET

### Co się dzieje

1. Turysta "przybywa" (proces zostaje utworzony)
2. Losowo decyduje jaki typ biletu chce kupić
3. Wysyła żądanie do Kasy
4. Czeka na odpowiedź

### Kod

```cpp
void TouristProcess::buyTicket() {
    // Zajmij slot w kolejce kasowej (flow control)
    sem_.wait(Semaphore::Index::CASHIER_QUEUE_SLOTS, 1, true);

    // Przygotuj żądanie
    TicketRequest request;
    request.touristId = tourist_.id;
    request.touristAge = tourist_.age;
    request.requestedType = selectTicketType();  // Losowo
    request.requestVip = tourist_.isVip;
    request.childCount = tourist_.childCount;
    for (uint32_t i = 0; i < tourist_.childCount; ++i) {
        request.childAges[i] = tourist_.childAges[i];
    }

    // Wyślij żądanie
    cashierQueue_.send(request, CashierMsgType::REQUEST);

    // Czekaj na odpowiedź (na unikalny typ dla tego turysty)
    auto response = cashierQueue_.receive(
        tourist_.id + CashierMsgType::RESPONSE_BASE);

    // Zwolnij slot
    sem_.post(Semaphore::Index::CASHIER_QUEUE_SLOTS, 1, true);

    if (response.has_value() && response->success) {
        tourist_.hasTicket = true;
        tourist_.ticketId = response->ticketId;
        tourist_.ticketType = response->ticketType;
        tourist_.ticketValidUntil = response->validUntil;

        // Zarejestruj w statystykach
        registerTourist();

        tourist_.state = TouristState::WAITING_ENTRY;
    }
}
```

### Dobór Biletu

| Typ biletu | Prawdopodobieństwo | Ważność |
|------------|-------------------|---------|
| SINGLE_USE | 30% | Jeden przejazd |
| TIME_TK1 | 25% | 1 godzina |
| TIME_TK2 | 20% | 2 godziny |
| TIME_TK3 | 15% | 4 godziny |
| DAILY | 10% | Cały dzień |

---

## Stan 2: WAITING_ENTRY

### Co się dzieje

1. Turysta staje w kolejce do bramki wejściowej
2. VIP ma priorytet (niższy mtype)
3. Czeka na zgodę od LowerStationWorker
4. Jeśli stacja pełna - czeka

### Kod

```cpp
void TouristProcess::waitForEntry() {
    // Wybierz typ kolejki (VIP lub zwykły)
    long requestType = tourist_.isVip
        ? EntryGateMsgType::VIP_REQUEST
        : EntryGateMsgType::REGULAR_REQUEST;

    // Zajmij slot w odpowiedniej kolejce
    uint8_t slotSem = tourist_.isVip
        ? Semaphore::Index::ENTRY_QUEUE_VIP_SLOTS
        : Semaphore::Index::ENTRY_QUEUE_REGULAR_SLOTS;
    sem_.wait(slotSem, 1, true);

    // Wyślij żądanie
    EntryGateRequest request;
    request.touristId = tourist_.id;
    request.touristPid = getpid();
    request.isVip = tourist_.isVip;
    entryGateQueue_.send(request, requestType);

    // Czekaj na odpowiedź
    auto response = entryGateQueue_.receive(
        tourist_.id + EntryGateMsgType::RESPONSE_BASE);

    sem_.post(slotSem, 1, true);

    if (response.has_value() && response->allowed) {
        Logger::info(Logger::Source::Tourist, tag_,
            "Entered station through gate");
        tourist_.state = TouristState::WAITING_BOARDING;
    } else {
        // Spróbuj ponownie później
        usleep(500000);  // 500ms
    }
}
```

### Mechanizm Priorytetu VIP

```
Kolejka wiadomości:
┌─────────────────────────────────────────────┐
│ [VIP,id=5] [REG,id=3] [VIP,id=8] [REG,id=1] │
│  mtype=1    mtype=2    mtype=1    mtype=2   │
└─────────────────────────────────────────────┘
                    │
                    ▼
Worker: msgrcv(queue, &msg, size, -2, 0)
        → Odbiera mtype <= 2, ale najniższy pierwszy
        → VIP (mtype=1) przed Regular (mtype=2)
```

---

## Stan 3: WAITING_BOARDING

### Co się dzieje

1. Turysta jest na terenie stacji dolnej
2. Dodaje się do kolejki oczekujących na krzesełko
3. LowerStationWorker przydziela krzesełka
4. Turysta polluje pamięć dzieloną czekając na przypisanie

### Kod

```cpp
void TouristProcess::waitForBoarding() {
    // Dodaj do kolejki w pamięci dzielonej
    BoardingQueueEntry entry;
    entry.touristId = tourist_.id;
    entry.touristPid = getpid();
    entry.slots = tourist_.slots;  // Ile miejsc potrzebuje
    entry.isVip = tourist_.isVip;
    entry.childCount = tourist_.childCount;
    entry.hasBike = tourist_.hasBike;

    {
        Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_CHAIRS);
        state_->chairPool.boardingQueue.addTourist(entry);
    }

    // Sygnalizuj pracownikowi że jest praca
    sem_.post(Semaphore::Index::BOARDING_QUEUE_WORK, 1, true);

    // Czekaj na przypisanie krzesełka (polling)
    while (!signals_.exit) {
        handleSignals();  // Obsłuż emergency stop

        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_CHAIRS);
            int32_t idx = state_->chairPool.boardingQueue.findTourist(tourist_.id);
            if (idx >= 0 && state_->chairPool.boardingQueue.entries[idx].readyToBoard) {
                tourist_.state = TouristState::ON_CHAIR;
                boardChair(state_->chairPool.boardingQueue.entries[idx].assignedChairId);
                state_->chairPool.boardingQueue.removeTourist(idx);
                break;
            }
        }

        usleep(100000);  // 100ms
    }
}
```

### Obliczanie Slotów

| Typ | Sloty |
|-----|-------|
| Pieszy | 1 |
| Rowerzysta z rowerem | 2 |
| Dziecko | +1 za każde |

```cpp
void Tourist::calculateSlots() {
    slots = 1;  // Podstawa
    if (type == TouristType::CYCLIST && hasBike) {
        slots = 2;
    }
    slots += childCount;
}
```

---

## Stan 4: ON_CHAIR

### Co się dzieje

1. Turysta "siada" na krzesełko
2. Symulowany czas przejazdu (usleep)
3. Po czasie - przejście do AT_TOP

### Kod

```cpp
void TouristProcess::rideChair() {
    Logger::info(Logger::Source::Tourist, tag_,
        "Boarding chair %d with %d slots",
        assignedChairId_, tourist_.slots);

    // Symulacja przejazdu
    time_t rideEnd = time(nullptr) + Config::Chair::RIDE_DURATION_US() / 1000000;

    while (time(nullptr) < rideEnd && !signals_.exit) {
        handleSignals();

        // Podczas emergency stop - czas nie płynie
        if (signals_.emergency) {
            rideEnd = time(nullptr) + (rideEnd - time(nullptr));
        }

        usleep(100000);  // 100ms
    }

    Logger::info(Logger::Source::Tourist, tag_, "Arrived at top station");
    tourist_.state = TouristState::AT_TOP;
    tourist_.ridesCompleted++;
}
```

---

## Stan 5: AT_TOP

### Co się dzieje

1. Turysta "wysiada" na stacji górnej
2. Wybiera drogę wyjścia:
   - Rowerzyści → trasy zjazdowe
   - Piesi → ścieżka piesza
3. Semafory limitują pojemność dróg

### Kod

```cpp
void TouristProcess::exitAtTop() {
    if (tourist_.type == TouristType::CYCLIST) {
        // Czekaj na wolne miejsce na trasie rowerowej
        sem_.wait(Semaphore::Index::EXIT_BIKE_TRAILS, 1, true);

        Logger::info(Logger::Source::Tourist, tag_,
            "Taking bike trail %s", toTrailCode(tourist_.preferredTrail));

        tourist_.state = TouristState::ON_TRAIL;
    } else {
        // Czekaj na ścieżkę pieszą
        sem_.wait(Semaphore::Index::EXIT_WALKING_PATH, 1, true);

        Logger::info(Logger::Source::Tourist, tag_, "Taking walking path");

        // Krótki spacer
        usleep(Constants::Delay::EXIT_ROUTE_TRANSITION_US);

        sem_.post(Semaphore::Index::EXIT_WALKING_PATH, 1, true);
        tourist_.state = TouristState::FINISHED;
    }
}
```

---

## Stan 6: ON_TRAIL (tylko rowerzyści)

### Co się dzieje

1. Rowerzysta zjeżdża wybraną trasą
2. Czas zależy od trudności (T1 < T2 < T3)
3. Po zjeździe - decyzja o kolejnej jeździe

### Kod

```cpp
void TouristProcess::rideTrail() {
    uint32_t duration = getDurationUs(tourist_.preferredTrail);

    Logger::info(Logger::Source::Tourist, tag_,
        "Riding trail %s for %u us", toTrailCode(tourist_.preferredTrail), duration);

    usleep(duration);

    // Zwolnij miejsce na trasie
    sem_.post(Semaphore::Index::EXIT_BIKE_TRAILS, 1, true);

    // Czy jechać ponownie?
    if (tourist_.canRideAgain() && tourist_.isTicketValid() && !isClosing()) {
        tourist_.state = TouristState::WAITING_ENTRY;
        // Wróć do kolejki
    } else {
        tourist_.state = TouristState::FINISHED;
    }
}
```

### Czasy Tras

| Trasa | Kod | Trudność | Czas (domyślny) |
|-------|-----|----------|-----------------|
| Easy | T1 | Łatwa | 1 sekunda |
| Medium | T2 | Średnia | 2 sekundy |
| Hard | T3 | Trudna | 3 sekundy |

---

## Stan 7: FINISHED

### Co się dzieje

1. Turysta opuszcza teren kolei
2. Aktualizuje statystyki
3. Proces kończy działanie

### Kod

```cpp
void TouristProcess::finish() {
    Logger::info(Logger::Source::Tourist, tag_,
        "Finished with %d rides completed", tourist_.ridesCompleted);

    // Dekrementuj licznik na stacji (jeśli byliśmy wewnątrz)
    {
        Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
        if (wasInsideStation_) {
            state_->operational.touristsInLowerStation--;
        }
    }

    // Zwolnij miejsce na stacji
    sem_.post(Semaphore::Index::STATION_CAPACITY, 1, true);
}
```

---

## Obsługa Dzieci

### Koncepcja

Dzieci poniżej 8 lat **muszą** jechać z dorosłym opiekunem. Są implementowane jako **wątki** w procesie rodzica:

```
┌─────────────────────────────────────┐
│        Tourist Process (Adult)      │
│                                     │
│  ┌───────────┐    ┌───────────┐     │
│  │  Child 1  │    │  Child 2  │     │
│  │  Thread   │    │  Thread   │     │
│  └───────────┘    └───────────┘     │
│                                     │
│  Wszystkie wątki są synchronizowane │
│  przez rodzica - jadą razem         │
└─────────────────────────────────────┘
```

### Walidacja

```cpp
// Dorosły może mieć max 2 dzieci
if (tourist_.childCount > Constants::Gate::MAX_CHILDREN_PER_ADULT) {
    Logger::error(..., "Too many children for one adult");
    tourist_.childCount = 2;
}

// Slots uwzględniają dzieci
tourist_.calculateSlots();  // adult(1) + children(2) = 3 slots
```

---

## Reagowanie na Emergency Stop

W każdym stanie turysta sprawdza flagi sygnałów:

```cpp
void TouristProcess::handleSignals() {
    if (signals_.emergency) {
        Logger::warn(Logger::Source::Tourist, tag_, "Emergency stop - waiting");

        while (!signals_.resume && !signals_.exit) {
            usleep(100000);  // Poll co 100ms
        }

        Logger::info(Logger::Source::Tourist, tag_, "Resuming after emergency");
        signals_.emergency = 0;
        signals_.resume = 0;
    }
}
```

---

Następny rozdział: [Protokół awaryjny](05_EMERGENCY_PROTOCOL.md)
