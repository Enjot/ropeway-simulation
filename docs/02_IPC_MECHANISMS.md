# Mechanizmy IPC - System V

## Wprowadzenie do System V IPC

System V IPC (Inter-Process Communication) to zestaw mechanizmów umożliwiających komunikację między procesami w systemach UNIX/POSIX. Projekt wykorzystuje trzy główne mechanizmy:

1. **Pamięć dzielona (Shared Memory)** - wspólny obszar pamięci
2. **Semafory** - synchronizacja dostępu
3. **Kolejki komunikatów (Message Queues)** - wymiana wiadomości

## Klucze IPC

Każdy zasób IPC identyfikowany jest przez **klucz** (`key_t`):

```cpp
// Generowanie kluczy za pomocą ftok()
key_t shmKey = ftok(".", 'S');  // Shared memory
key_t semKey = ftok(".", 'M');  // Semaphores
key_t msgKey = ftok(".", 'W');  // Worker messages
```

**Uwaga:** `ftok()` generuje klucz na podstawie ścieżki i znaku. Ta sama kombinacja zawsze da ten sam klucz.

---

## 1. Pamięć Dzielona (Shared Memory)

### Koncepcja

Pamięć dzielona to segment pamięci widoczny dla wielu procesów. Zmiany dokonane przez jeden proces są natychmiast widoczne dla innych.

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  Process A  │     │  Process B  │     │  Process C  │
└──────┬──────┘     └──────┬──────┘     └──────┬──────┘
       │                   │                   │
       └───────────────────┼───────────────────┘
                           │
                    ┌──────┴──────┐
                    │   SHARED    │
                    │   MEMORY    │
                    │  SEGMENT    │
                    └─────────────┘
```

### API System V

```cpp
// Tworzenie segmentu
int shmId = shmget(key, sizeof(SharedData), IPC_CREAT | 0600);

// Dołączanie do segmentu
void* ptr = shmat(shmId, nullptr, 0);

// Odłączanie
shmdt(ptr);

// Usuwanie segmentu
shmctl(shmId, IPC_RMID, nullptr);
```

### Wrapper w Projekcie: `SharedMemory<T>`

```cpp
// Tworzenie (proces główny)
auto shm = SharedMemory<SharedRopewayState>::create(shmKey);

// Dołączanie (procesy potomne)
SharedMemory<SharedRopewayState> shm(shmKey, true);

// Dostęp do danych
shm->operational.state = RopewayState::RUNNING;
shm->chairPool.chairsInUse++;
```

### Struktura Pamięci Dzielonej

```cpp
struct SharedRopewayState {
    SharedOperationalState operational;  // Stan operacyjny
    SharedChairPoolState chairPool;      // Pula krzesełek
    SharedStatisticsState stats;         // Statystyki
};
```

**Ważne:** Dostęp do pamięci dzielonej MUSI być chroniony semaforami!

---

## 2. Semafory

### Koncepcja

Semafor to licznik używany do synchronizacji. Pozwala kontrolować dostęp do zasobów współdzielonych.

- **wait()** (P) - zmniejsza wartość, blokuje jeśli 0
- **post()** (V) - zwiększa wartość, odblokowuje czekających

```
Semafor = 1 (dostępny)
     │
     ▼
┌──────────┐
│ wait()   │ ─── Semafor = 0 (zajęty)
└──────────┘
     │
     ▼
[Sekcja krytyczna]
     │
     ▼
┌──────────┐
│ post()   │ ─── Semafor = 1 (wolny)
└──────────┘
```

### API System V

```cpp
// Tworzenie zestawu semaforów
int semId = semget(key, TOTAL_SEMAPHORES, IPC_CREAT | 0600);

// Inicjalizacja wartości
union semun arg;
arg.val = 1;
semctl(semId, index, SETVAL, arg);

// Operacje wait/post
struct sembuf op;
op.sem_num = index;
op.sem_op = -1;  // wait (lub +1 dla post)
op.sem_flg = SEM_UNDO;
semop(semId, &op, 1);
```

### Wrapper w Projekcie: `Semaphore`

```cpp
Semaphore sem(semKey);

// Inicjalizacja
sem.initialize(Semaphore::Index::STATION_CAPACITY, 20);

// Operacje
sem.wait(Semaphore::Index::STATION_CAPACITY, 1, true);   // Blokujące
sem.tryAcquire(Semaphore::Index::SHM_OPERATIONAL, 1, true); // Nieblokujące
sem.post(Semaphore::Index::STATION_CAPACITY, 1, true);

// RAII Lock
{
    Semaphore::ScopedLock lock(sem, Semaphore::Index::SHM_OPERATIONAL);
    // Sekcja krytyczna
} // Automatyczne post()
```

### Semafory w Projekcie

| Indeks | Nazwa | Wartość początkowa | Przeznaczenie |
|--------|-------|-------------------|---------------|
| 0 | LOGGER_READY | 0 | Sygnalizacja gotowości loggera |
| 1 | CASHIER_READY | 0 | Sygnalizacja gotowości kasy |
| 2 | LOWER_WORKER_READY | 0 | Gotowość pracownika dolnego |
| 3 | UPPER_WORKER_READY | 0 | Gotowość pracownika górnego |
| 7 | STATION_CAPACITY | N | Limit osób na stacji |
| 9 | CHAIRS_AVAILABLE | 36 | Dostępne krzesełka |
| 14 | SHM_OPERATIONAL | 1 | Mutex na stan operacyjny |
| 15 | SHM_CHAIRS | 1 | Mutex na pulę krzesełek |
| 16 | SHM_STATS | 1 | Mutex na statystyki |

### Kolejność Lockowania (Zapobieganie Deadlock)

**ZAWSZE lockuj w tej kolejności:**
```
SHM_OPERATIONAL → SHM_CHAIRS → SHM_STATS
```

```cpp
// POPRAWNIE:
{
    Semaphore::ScopedLock lockOp(sem, Semaphore::Index::SHM_OPERATIONAL);
    Semaphore::ScopedLock lockChairs(sem, Semaphore::Index::SHM_CHAIRS);
    // ...
}

// NIEPOPRAWNIE (może spowodować deadlock):
{
    Semaphore::ScopedLock lockChairs(sem, Semaphore::Index::SHM_CHAIRS);
    Semaphore::ScopedLock lockOp(sem, Semaphore::Index::SHM_OPERATIONAL);  // ZŁE!
}
```

### SEM_UNDO

Flaga `SEM_UNDO` zapewnia automatyczne cofnięcie operacji semaforowych gdy proces się zakończy (nawet awaryjnie):

```cpp
sem.wait(index, 1, true);  // true = SEM_UNDO
// Jeśli proces się zawiesi, kernel automatycznie wykona post()
```

---

## 3. Kolejki Komunikatów (Message Queues)

### Koncepcja

Kolejka komunikatów to bufor FIFO do wymiany wiadomości między procesami. Każda wiadomość ma **typ** używany do filtrowania.

```
        ┌─────────────────────────────────────────┐
Send ──>│ [msg1] [msg2] [msg3] [msg4] [msg5] ... │──> Receive
        └─────────────────────────────────────────┘
```

### API System V

```cpp
// Tworzenie kolejki
int msgId = msgget(key, IPC_CREAT | 0600);

// Struktura wiadomości (musi zaczynać się od long mtype)
struct Message {
    long mtype;      // Typ wiadomości (musi być > 0)
    char data[256];  // Dane
};

// Wysyłanie
msgsnd(msgId, &msg, sizeof(msg.data), 0);

// Odbieranie
msgrcv(msgId, &msg, sizeof(msg.data), type, 0);
// type > 0: dokładny typ
// type = 0: dowolny typ
// type < 0: typ <= |type| (priorytet)

// Usuwanie
msgctl(msgId, IPC_RMID, nullptr);
```

### Wrapper w Projekcie: `MessageQueue<T>`

```cpp
MessageQueue<TicketRequest> cashierQueue(cashierMsgKey, "Cashier");

// Wysyłanie
TicketRequest req;
req.touristId = 1;
cashierQueue.send(req, CashierMsgType::REQUEST);

// Odbieranie (blokujące)
auto response = cashierQueue.receive(touristId + CashierMsgType::RESPONSE_BASE);

// Odbieranie (nieblokujące)
auto msg = cashierQueue.tryReceive(type);
if (msg.has_value()) {
    // Obsłuż wiadomość
}
```

### Kolejki w Projekcie

| Kolejka | Typ wiadomości | Kierunek | Przeznaczenie |
|---------|---------------|----------|---------------|
| WorkerQueue | WorkerMessage | Worker ↔ Worker | Protokół awaryjny |
| CashierQueue | TicketRequest/Response | Tourist ↔ Cashier | Zakup biletów |
| EntryGateQueue | EntryGateRequest/Response | Tourist ↔ LowerWorker | Wejście na stację |
| LogQueue | LogMessage | All → Logger | Centralne logowanie |

### Priorytet VIP (Sprytne użycie mtype)

```cpp
// Typy wiadomości dla bramki wejściowej
namespace EntryGateMsgType {
    constexpr long VIP_REQUEST = 1;      // VIP ma niższy typ
    constexpr long REGULAR_REQUEST = 2;  // Zwykły turysta
}

// Worker odbiera z priorytetem:
auto msg = queue.receive(-2);  // Odbiera typ 1 lub 2, ale 1 (VIP) pierwszy!
```

---

## Obsługa Błędów i EINTR

Wszystkie operacje IPC mogą być przerwane przez sygnał (EINTR). Wrapper obsługuje to automatycznie:

```cpp
bool Semaphore::wait(uint8_t semIndex, int32_t n, bool useUndo) const {
    struct sembuf op;
    op.sem_num = semIndex;
    op.sem_op = -n;
    op.sem_flg = useUndo ? SEM_UNDO : 0;

    while (semop(semId_, &op, 1) == -1) {
        if (errno == EINTR) continue;  // Przerwane przez sygnał, ponów
        throw ipc_exception("semop failed");
    }
    return true;
}
```

---

## Ograniczenia macOS

Na macOS kolejki komunikatów mają ograniczone limity:
- **MSGMNB** = 2048 bajtów na kolejkę
- **msgtql** = 40 wiadomości w całym systemie

Dlatego używamy semaforów do kontroli przepływu:

```cpp
// Przed wysłaniem
sem.wait(Semaphore::Index::CASHIER_QUEUE_SLOTS, 1, true);
queue.send(request, type);

// Po odebraniu
auto msg = queue.receive(type);
sem.post(Semaphore::Index::CASHIER_QUEUE_SLOTS, 1, true);
```

---

## Czyszczenie Zasobów

Zasoby IPC **nie są automatycznie usuwane** po zakończeniu procesu. IpcManager rejestruje handler `atexit()`:

```cpp
// W destruktorze IpcManager
void cleanup() noexcept {
    shm_.destroy();      // shmctl(id, IPC_RMID, nullptr)
    sem_.destroy();      // semctl(id, 0, IPC_RMID)
    workerQueue_.destroy();  // msgctl(id, IPC_RMID, nullptr)
    // ...
}
```

**Jeśli program się zawiesi**, użyj `ipcs` i `ipcrm`:

```bash
# Lista zasobów IPC
ipcs

# Usunięcie
ipcrm -m <shmid>  # Shared memory
ipcrm -s <semid>  # Semaphores
ipcrm -q <msgid>  # Message queues
```

---

## Podsumowanie

| Mechanizm | Użycie | Zalety | Wady |
|-----------|--------|--------|------|
| Shared Memory | Współdzielony stan | Szybki dostęp | Wymaga synchronizacji |
| Semafory | Synchronizacja | Niezawodne | Możliwy deadlock |
| Message Queues | Komunikacja | Typowane wiadomości | Limity rozmiaru |

Następny rozdział: [Procesy](03_PROCESSES.md)
