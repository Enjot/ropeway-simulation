# Słownik Pojęć

## A

### Async-Signal-Safe
Funkcja, która może być bezpiecznie wywołana z handlera sygnału. Przykłady: `write()`, `_exit()`. Przeciwieństwa: `printf()`, `malloc()`.

## B

### Bramka Jazdy (Ride Gate)
Jedna z 3 bramek kontrolujących wejście na peron, gdzie turyści wsiadają na krzesełka. Otwierana przez pracownika.

### Bramka Wstępu (Entry Gate)
Jedna z 4 bramek kontrolujących wejście na teren dolnej stacji. Sprawdza ważność biletu.

## C

### Counting Semaphore
Semafor zliczający - może mieć wartość > 1. Używany do kontroli pojemności (np. max N osób na stacji).

## D

### Deadlock (Zakleszczenie)
Sytuacja gdy dwa lub więcej procesów czeka na siebie nawzajem, uniemożliwiając postęp. Przykład: A czeka na lock B, B czeka na lock A.

## E

### EINTR
Kod błędu errno oznaczający przerwanie operacji przez sygnał. Należy ponowić operację.

### Emergency Stop
Awaryjne zatrzymanie kolei. Inicjowane przez pracownika gdy wykryje zagrożenie. Wszystkie procesy wstrzymują działanie.

### exec()
Funkcja systemowa zastępująca bieżący proces nowym programem. Używana z `fork()` do tworzenia nowych procesów.

## F

### Flow Control
Kontrola przepływu - mechanizm zapobiegający przepełnieniu kolejek komunikatów przez ograniczenie liczby wysyłanych wiadomości.

### fork()
Funkcja systemowa tworząca kopię bieżącego procesu (proces potomny).

### ftok()
Funkcja generująca klucz IPC z ścieżki pliku i znaku. `ftok(".", 'S')` → klucz dla shared memory.

## G

### Guardian (Opiekun)
Osoba dorosła opiekująca się dzieckiem poniżej 8 lat. Musi jechać na tym samym krzesełku.

## I

### IPC (Inter-Process Communication)
Komunikacja międzyprocesowa - mechanizmy pozwalające procesom wymieniać dane i synchronizować się.

## K

### key_t
Typ danych reprezentujący klucz IPC. Używany do identyfikacji zasobów System V (shm, sem, msg).

### Krzesełko (Chair)
Jednostka transportowa kolei linowej. Każde ma 4 sloty (miejsca). Max 36 jednocześnie w użyciu.

## L

### Lock Ordering
Ustalona kolejność lockowania wielu zasobów. Zapobiega deadlockom. W projekcie: OPERATIONAL → CHAIRS → STATS.

## M

### Message Queue
Kolejka komunikatów System V. Pozwala procesom wymieniać typowane wiadomości. API: msgget, msgsnd, msgrcv.

### mtype
Typ wiadomości w kolejce komunikatów. Używany do filtrowania i priorytetów. Ujemny mtype = priorytet.

### Mutex
Mutual Exclusion - mechanizm zapewniający że tylko jeden proces może być w sekcji krytycznej. W projekcie implementowany przez semafor binarny.

## P

### Polling
Technika aktywnego sprawdzania stanu w pętli. Używana gdy nie ma możliwości powiadomienia zdarzeniowego.

### PID (Process ID)
Unikalny identyfikator procesu w systemie. Zwracany przez `fork()` i `getpid()`.

## R

### Race Condition (Wyścig)
Błąd gdy wynik zależy od kolejności wykonania operacji przez współbieżne procesy. Rozwiązanie: synchronizacja.

### RAII
Resource Acquisition Is Initialization - wzorzec C++ gdzie zasoby są alokowane w konstruktorze i zwalniane w destruktorze.

## S

### Sekcja Krytyczna
Fragment kodu który może być wykonywany tylko przez jeden proces naraz. Chroniony przez mutex/semafor.

### SEM_UNDO
Flaga semaforowa powodująca automatyczne cofnięcie operacji gdy proces się zakończy. Zapobiega "wiszącym" lockom.

### Semaphore
Semafor System V - mechanizm synchronizacji. API: semget, semop, semctl. Wartość 0 = zablokowany.

### Shared Memory
Pamięć dzielona System V - segment pamięci widoczny dla wielu procesów. API: shmget, shmat, shmdt, shmctl.

### Signal
Sygnał POSIX - asynchroniczne powiadomienie do procesu. Przykłady: SIGTERM (zakończ), SIGUSR1 (użytkownika).

### Slot
Miejsce na krzesełku. Pieszy = 1 slot, rowerzysta z rowerem = 2 sloty. Każde dziecko = 1 dodatkowy slot.

### Starvation (Zagłodzenie)
Sytuacja gdy proces nigdy nie dostaje zasobu bo inni go ciągle zajmują. Rozwiązanie: fair scheduling.

## T

### TIME_SCALE
Współczynnik przeskalowania czasu. 1 sekunda rzeczywista = TIME_SCALE sekund symulowanych.

### Tourist
Turysta - główny aktor w symulacji. Każdy turysta to osobny proces przechodzący przez cykl życia.

### Trail (Trasa)
Trasa zjazdowa dla rowerzystów. Trzy poziomy trudności: T1 (łatwa), T2 (średnia), T3 (trudna).

## V

### VIP
Status turysty umożliwiający wejście bez kolejki. Ok. 1% turystów to VIP.

### volatile
Słowo kluczowe C++ mówiące kompilatorowi że zmienna może być zmieniona poza bieżącym przepływem (np. przez handler sygnału).

## W

### Worker
Pracownik stacji. Dolny (LowerStationWorker) zarządza bramkami i krzesełkami. Górny (UpperStationWorker) zarządza wyjściami.

## Z

### Zombie Process
Proces który się zakończył ale nie został "zebrany" przez rodzica (brak wait()). Rozwiązanie: SIGCHLD=SIG_IGN lub wait().

---

## Skróty

| Skrót | Pełna nazwa | Znaczenie |
|-------|-------------|-----------|
| IPC | Inter-Process Communication | Komunikacja międzyprocesowa |
| SHM | Shared Memory | Pamięć dzielona |
| SEM | Semaphore | Semafor |
| MSG | Message Queue | Kolejka komunikatów |
| PID | Process ID | Identyfikator procesu |
| RAII | Resource Acquisition Is Initialization | Wzorzec zarządzania zasobami |
| VIP | Very Important Person | Uprzywilejowany turysta |

---

## Symbole w Diagramach

```
──────>  Przepływ danych/kontroli
- - - >  Przepływ opcjonalny
[TEXT]   Wiadomość/Operacja
┌─────┐
│     │  Proces/Komponent
└─────┘
   ▼     Kierunek przepływu (dół)
   ▲     Kierunek przepływu (góra)
```
