# Synchronizacja i Wzorce Współbieżności

## Wprowadzenie

W systemie wieloprocesowym synchronizacja jest kluczowa dla:
- Unikania wyścigów (race conditions)
- Zapobiegania zakleszczeniom (deadlocks)
- Zagwarantowania spójności danych

Ten dokument opisuje wzorce synchronizacji używane w projekcie.

---

## Problem Wyścigu (Race Condition)

### Przykład Problemu

```cpp
// Proces A                      // Proces B
counter = shm->counter;          counter = shm->counter;
counter++;                       counter++;
shm->counter = counter;          shm->counter = counter;

// Oczekiwany wynik: counter += 2
// Rzeczywisty wynik: counter += 1 (jeśli operacje się przeplatają!)
```

### Rozwiązanie: Sekcja Krytyczna

```cpp
// Proces A                      // Proces B
sem.wait(MUTEX);                 sem.wait(MUTEX);  // Blokuje!
shm->counter++;                  // Czeka...
sem.post(MUTEX);                 shm->counter++;
                                 sem.post(MUTEX);
```

---

## Semafory jako Mutexy

### RAII Pattern: ScopedLock

```cpp
class Semaphore::ScopedLock {
public:
    ScopedLock(const Semaphore& sem, uint8_t index)
        : sem_(sem), index_(index)
    {
        sem_.wait(index_, 1, true);  // Acquire
    }

    ~ScopedLock() {
        sem_.post(index_, 1, true);  // Release
    }

private:
    const Semaphore& sem_;
    uint8_t index_;
};
```

### Użycie

```cpp
void incrementCounter() {
    Semaphore::ScopedLock lock(sem, Semaphore::Index::SHM_OPERATIONAL);
    // Sekcja krytyczna - tylko jeden proces naraz
    state->operational.touristsInLowerStation++;
}  // Automatyczny post() w destruktorze
```

---

## Unikanie Deadlocków

### Problem

```
Proces A:                    Proces B:
lock(SHM_CHAIRS)             lock(SHM_OPERATIONAL)
   lock(SHM_OPERATIONAL)        lock(SHM_CHAIRS)
   // DEADLOCK!                 // DEADLOCK!
```

### Rozwiązanie: Ustalona Kolejność

**Zawsze lockuj w tej kolejności:**
```
SHM_OPERATIONAL → SHM_CHAIRS → SHM_STATS
```

```cpp
// POPRAWNIE:
{
    Semaphore::ScopedLock lock1(sem, Semaphore::Index::SHM_OPERATIONAL);
    Semaphore::ScopedLock lock2(sem, Semaphore::Index::SHM_CHAIRS);
    // Bezpieczne - zawsze ta sama kolejność
}

// NIEPOPRAWNIE:
{
    Semaphore::ScopedLock lock1(sem, Semaphore::Index::SHM_CHAIRS);
    Semaphore::ScopedLock lock2(sem, Semaphore::Index::SHM_OPERATIONAL);
    // RYZYKO DEADLOCK!
}
```

---

## Semafory Zliczające (Counting Semaphores)

### Kontrola Pojemności

```cpp
// Inicjalizacja
sem.initialize(Semaphore::Index::STATION_CAPACITY, 20);  // Max 20 osób

// Wejście turysty
sem.wait(STATION_CAPACITY, 1, true);  // Dekrementuj, blokuj jeśli 0
enterStation();

// Wyjście turysty
leaveStation();
sem.post(STATION_CAPACITY, 1, true);  // Inkrementuj
```

### Diagram

```
Pojemność = 3

Turysta 1: wait() → Pojemność = 2 → Wchodzi
Turysta 2: wait() → Pojemność = 1 → Wchodzi
Turysta 3: wait() → Pojemność = 0 → Wchodzi
Turysta 4: wait() → BLOKUJE (Pojemność = 0)
          ...
Turysta 1: post() → Pojemność = 1
Turysta 4: Odblokowany → Pojemność = 0 → Wchodzi
```

---

## Flow Control dla Kolejek

### Problem

Kolejki komunikatów mają ograniczony rozmiar. Wysyłanie do pełnej kolejki:
- Blokuje (domyślnie)
- Lub zwraca błąd (IPC_NOWAIT)

### Rozwiązanie: Semafory jako Sloty

```cpp
// Inicjalizacja
sem.initialize(CASHIER_QUEUE_SLOTS, 5);  // 5 slotów

// Wysyłający (Tourist)
sem.wait(CASHIER_QUEUE_SLOTS, 1, true);  // Zajmij slot
cashierQueue.send(request, type);
// Slot zwolniony dopiero po otrzymaniu odpowiedzi

// Po otrzymaniu odpowiedzi
sem.post(CASHIER_QUEUE_SLOTS, 1, true);  // Zwolnij slot
```

### Diagram Przepływu

```
Sloty: [■][■][■][□][□]  (3 zajęte, 2 wolne)
           │
Tourist 4: wait() → Slot zajęty
           │
           ▼
Sloty: [■][■][■][■][□]  (4 zajęte)
           │
Tourist 5: wait() → Slot zajęty
           │
           ▼
Sloty: [■][■][■][■][■]  (5 zajętych)
           │
Tourist 6: wait() → BLOKUJE
           │
           ▼
Tourist 1: post() → Slot zwolniony
           │
           ▼
Sloty: [□][■][■][■][■]  (4 zajęte)
Tourist 6: Odblokowany
```

---

## Priorytet w Kolejkach (VIP)

### Mechanizm

Kolejki komunikatów System V wspierają priorytety poprzez `mtype`:

```cpp
// Typy wiadomości
VIP_REQUEST = 1      // Niższy typ = wyższy priorytet
REGULAR_REQUEST = 2

// Odbieranie z priorytetem
msgrcv(queue, &msg, size, -2, 0);
// -2 oznacza: odbierz wiadomość z mtype <= 2, najniższy pierwszy
```

### Przykład

```
Kolejka: [VIP,3] [REG,1] [VIP,5] [REG,2]
          mtype=1  mtype=2  mtype=1  mtype=2

msgrcv(queue, &msg, size, -2, 0);
→ Odbiera: [VIP,3] (mtype=1, najniższy)

msgrcv(queue, &msg, size, -2, 0);
→ Odbiera: [VIP,5] (mtype=1)

msgrcv(queue, &msg, size, -2, 0);
→ Odbiera: [REG,1] (mtype=2)
```

---

## Synchronizacja Startu

### Problem

Procesy robocze muszą być gotowe przed rozpoczęciem symulacji.

### Rozwiązanie: Semafory Sygnalizacyjne

```cpp
// Inicjalizacja (wartość 0)
sem.initialize(CASHIER_READY, 0);
sem.initialize(LOWER_WORKER_READY, 0);
sem.initialize(UPPER_WORKER_READY, 0);

// W procesie Cashier (po inicjalizacji)
sem.post(CASHIER_READY, 1, true);  // "Jestem gotowy!"

// W głównym procesie
sem.wait(CASHIER_READY, 1, true);       // Czekaj na kasę
sem.wait(LOWER_WORKER_READY, 1, true);  // Czekaj na dolnego
sem.wait(UPPER_WORKER_READY, 1, true);  // Czekaj na górnego
// Teraz wszystkie są gotowe
```

### Diagram

```
Main:     spawn(Cashier) → spawn(Workers) → wait(READY signals)
                │              │                    ▲
                │              │                    │
                ▼              ▼                    │
Cashier:  init... → post(CASHIER_READY) ───────────┘
Workers:  init... → post(WORKER_READY) ────────────┘
```

---

## SEM_UNDO - Automatyczne Cofnięcie

### Problem

Jeśli proces się zawiesi trzymając lock, inne procesy będą czekać wiecznie.

### Rozwiązanie: SEM_UNDO

```cpp
// Z flagą SEM_UNDO
sem.wait(MUTEX, 1, true);  // true = SEM_UNDO

// Jeśli proces zakończy się (nawet awaryjnie):
// Kernel automatycznie wykona post() dla wszystkich operacji z SEM_UNDO
```

### Kiedy Używać

| Sytuacja | SEM_UNDO |
|----------|----------|
| Mutex (lock/unlock) | TAK |
| Counting (wejście/wyjście) | TAK |
| Sygnalizacja jednorazowa | NIE |

---

## Wzorzec: Polling z Pamięcią Dzieloną

### Kiedy Użyć

Gdy proces musi czekać na zmianę stanu, której nie można łatwo zsygnalizować.

### Implementacja

```cpp
void TouristProcess::waitForChairAssignment() {
    while (!signals_.exit) {
        // Sprawdź flagi sygnałów (emergency stop)
        handleSignals();

        // Sprawdź pamięć dzieloną
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_CHAIRS);
            int idx = boardingQueue.findTourist(tourist_.id);
            if (idx >= 0 && boardingQueue.entries[idx].readyToBoard) {
                // Znaleziono przypisanie!
                chairId = boardingQueue.entries[idx].assignedChairId;
                break;
            }
        }

        // Nie znaleziono - poczekaj i spróbuj ponownie
        usleep(100000);  // 100ms
    }
}
```

### Uwagi

- **Nie używaj busy-wait** (ciągłe sprawdzanie bez przerwy)
- **Używaj usleep()** między sprawdzeniami
- **Obsługuj sygnały** w pętli

---

## Wzorzec: Request-Response przez Kolejkę

### Implementacja

```cpp
// Wysyłający (Tourist)
TicketRequest req;
req.touristId = myId;
// ...

// Wyślij żądanie
queue.send(req, CashierMsgType::REQUEST);

// Czekaj na odpowiedź z unikalnym typem
auto response = queue.receive(myId + CashierMsgType::RESPONSE_BASE);

// Odbierający (Cashier)
while (running) {
    auto req = queue.receive(CashierMsgType::REQUEST);

    // Przetwórz żądanie
    TicketResponse resp = processRequest(*req);

    // Odpowiedz na unikalny typ
    queue.send(resp, req->touristId + RESPONSE_BASE);
}
```

### Diagram

```
Tourist 5:
    send(REQUEST, type=1) ─────────────────┐
                                           │
    receive(type=1005) ◄───────────────┐   │
                                       │   │
                                       │   ▼
Cashier:                              send(RESPONSE, type=1005)
    receive(type=1) ◄──────────────────────┘
    process()
    send(type=1005) ───────────────────────┘
```

---

## Obsługa EINTR

### Problem

Operacje IPC mogą być przerwane przez sygnał:

```cpp
int result = semop(semId, &op, 1);
if (result == -1 && errno == EINTR) {
    // Przerwane przez sygnał - co teraz?
}
```

### Rozwiązanie: Retry Loop

```cpp
bool Semaphore::wait(uint8_t index, int32_t n, bool useUndo) const {
    struct sembuf op;
    op.sem_num = index;
    op.sem_op = -n;
    op.sem_flg = useUndo ? SEM_UNDO : 0;

    while (semop(semId_, &op, 1) == -1) {
        if (errno == EINTR) {
            // Przerwane przez sygnał - ponów
            continue;
        }
        // Inny błąd - rzuć wyjątek
        throw ipc_exception("semop failed");
    }
    return true;
}
```

---

## Podsumowanie Wzorców

| Wzorzec | Użycie | Mechanizm |
|---------|--------|-----------|
| Mutex | Ochrona danych | Semafor binarny + ScopedLock |
| Counting | Kontrola pojemności | Semafor zliczający |
| Flow Control | Ograniczenie kolejki | Semafor = sloty |
| Priority | VIP first | mtype w kolejce |
| Sync Start | Czekanie na gotowość | Semafor = 0 → post |
| Request-Response | Komunikacja | Unikalny mtype na odpowiedź |
| Polling | Czekanie na stan | usleep + sprawdzenie |

---

## Typowe Błędy

### 1. Brak Locka

```cpp
// ŹLE:
state->counter++;

// DOBRZE:
{
    Semaphore::ScopedLock lock(sem, INDEX);
    state->counter++;
}
```

### 2. Niepoprawna Kolejność

```cpp
// ŹLE:
lock(B);
lock(A);  // Może deadlock jeśli inny proces robi lock(A), lock(B)

// DOBRZE:
lock(A);  // Zawsze najpierw A
lock(B);  // Potem B
```

### 3. Zapomniany Post

```cpp
// ŹLE:
sem.wait(INDEX);
if (error) return;  // Zapomniany post!

// DOBRZE:
{
    Semaphore::ScopedLock lock(sem, INDEX);
    if (error) return;  // Destruktor zrobi post
}
```

### 4. Brak SEM_UNDO

```cpp
// ŹLE:
sem.wait(INDEX, 1, false);  // Brak SEM_UNDO
// Jeśli proces się zawiesi, lock zostanie na zawsze

// DOBRZE:
sem.wait(INDEX, 1, true);  // SEM_UNDO
```

---

Następny rozdział: [Uruchamianie i Testowanie](07_RUNNING_AND_TESTING.md)
