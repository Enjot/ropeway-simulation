#pragma once

#include <sys/types.h>
#include <time.h>

// ============================================================================
// Constants
// ============================================================================

#define CHAIR_CAPACITY 4          // Max slots per chair (walkers=1, cyclists=2)
#define MAX_CHAIRS_IN_TRANSIT 36  // Maximum chairs simultaneously in use
#define TOTAL_CHAIRS 72           // Total chairs in the system
#define ENTRY_GATES 4             // Number of entry gates at lower station
#define EXIT_GATES 2              // Number of exit gates at upper station
#define PLATFORM_GATES 3          // Number of platform gates (before boarding)
#define MAX_KIDS_PER_ADULT 2      // Maximum kids per guardian

// Semaphore indices in the semaphore set
#define SEM_STATE 0           // Mutex for SharedState access
#define SEM_STATS 1           // Mutex for statistics access
#define SEM_ENTRY_GATES 2     // Entry gates capacity (4)
#define SEM_EXIT_GATES 3      // Exit gates capacity (2)
#define SEM_LOWER_STATION 4   // Lower station capacity (N)
#define SEM_CHAIRS 5          // Chair availability (36)
#define SEM_WORKER_READY 6    // (DEPRECATED - unused)
#define SEM_PAUSE 7           // Pause sync (SIGTSTP)
#define SEM_PLATFORM_GATES 8  // Platform gates capacity (3)
#define SEM_EMERGENCY_CLEAR 9 // Released when emergency cleared (for tourist waiters)
#define SEM_COUNT 10          // Total number of semaphores

// Message queue IDs (for ftok project_id)
#define MQ_CASHIER_ID 1       // Tourist <-> Cashier
#define MQ_PLATFORM_ID 2      // Tourist -> Lower Worker (ready to board)
#define MQ_BOARDING_ID 3      // Lower Worker -> Tourist (boarding confirmation)
#define MQ_ARRIVALS_ID 4      // Tourist -> Upper Worker (arrival notification)
#define MQ_WORKER_ID 5        // Worker <-> Worker emergency communication

// Worker message types (for WorkerMsg.msg_type)
#define WORKER_MSG_READY_TO_RESUME 1  // Detecting worker -> Receiving worker
#define WORKER_MSG_I_AM_READY 2       // Receiving worker -> Detecting worker

// Worker message destination (for WorkerMsg.mtype)
#define WORKER_DEST_LOWER 1           // Message to lower worker
#define WORKER_DEST_UPPER 2           // Message to upper worker

// ============================================================================
// Enums
// ============================================================================

typedef enum {
    TOURIST_WALKER = 0,
    TOURIST_CYCLIST = 1
} TouristType;

typedef enum {
    TICKET_SINGLE = 0,    // Single-use
    TICKET_TIME_T1 = 1,   // Time-based (short)
    TICKET_TIME_T2 = 2,   // Time-based (medium)
    TICKET_TIME_T3 = 3,   // Time-based (long)
    TICKET_DAILY = 4,     // Daily pass
    TICKET_COUNT = 5
} TicketType;

typedef enum {
    TRAIL_WALK = 0,       // Walking trail
    TRAIL_BIKE_FAST = 1,  // Fast bike trail
    TRAIL_BIKE_MEDIUM = 2,// Medium bike trail
    TRAIL_BIKE_SLOW = 3   // Slow bike trail
} TrailType;

typedef enum {
    STAGE_AT_CASHIER = 0,               // Initial: at cashier (buying ticket after spawn)
    STAGE_AT_ENTRY_GATES = 1,           // Sync 0: at entry gates (trying to pass)
    STAGE_ENTERED_LOWER_STATION = 2,    // Sync 1: just entered lower station waiting room
    STAGE_QUEUED_FOR_PLATFORM = 3,      // Sync 2: waiting in queue for platform access
    STAGE_AT_LOWER_PLATFORM = 4,        // Sync 3: at lower platform (ready to board)
    STAGE_ON_CHAIR = 5,                 // Sync 4: on the chairlift
    STAGE_RIDE_COMPLETE = 6,            // Sync 5: ride complete, exiting chair
    STAGE_AT_UPPER_PLATFORM_GATES = 7,  // Sync 6: queued at upper platform exit gates
    STAGE_ON_TRAIL = 8,                 // Sync 7: on trail (descending back to entry gates)
    STAGE_LEAVING = 9                   // Terminal: leaving (ticket invalid, station closing, or done)
} TouristStage;

// Cashier message queue mtype values
typedef enum {
    MSG_CASHIER_REQUEST = 1,            // All tourists send requests with this mtype
    MSG_CASHIER_RESPONSE_BASE = 1000    // Responses use BASE + tourist_id
} CashierMsgType;

// ============================================================================
// Per-Tourist Tracking Entry
// ============================================================================

typedef struct {
    int active;                     // 1 if this slot is in use
    int tourist_id;                 // Tourist ID
    int ticket_type;                // TicketType
    int entry_time_sim;             // Simulated time of first entry (minutes from midnight)
    int total_rides;                // Number of rides completed
    int is_vip;                     // VIP status
    TouristType tourist_type;       // Walker/cyclist
    int kid_count;                  // Number of kids (for family tracking)
} TouristEntry;

// ============================================================================
// Shared Memory Structure
// ============================================================================

typedef struct {
    // Time management
    time_t real_start_time;         // When simulation started (real time)
    int sim_start_minutes;          // 480 = 08:00 (minutes from midnight)
    int sim_end_minutes;            // 1020 = 17:00 (minutes from midnight)
    double time_acceleration;       // Sim minutes per real second
    int chair_travel_time_sim;      // Simulated minutes for ride

    // SIGTSTP/SIGCONT pause handling
    time_t pause_start_time;        // When SIGTSTP received (0 = not paused)
    time_t total_pause_offset;      // Accumulated pause time in seconds

    // Simulation state (protected by SEM_STATE)
    int running;                    // 0 = shutdown
    int closing;                    // 1 = stop accepting new tourists
    int emergency_stop;             // 1 = chairlift stopped (SIGUSR1)
    int paused;                     // 1 = simulation paused (SIGTSTP)
    int pause_waiters;              // Count of processes waiting on SEM_PAUSE (issue #7 fix)
    int emergency_waiters;          // Count of processes waiting on SEM_EMERGENCY_CLEAR (issue #4 fix)

    // Statistics (protected by SEM_STATS)
    int total_tourists;             // Total tourists spawned
    int total_rides;                // Total rides completed
    int rides_by_ticket[TICKET_COUNT]; // Rides per ticket type
    int tourists_by_ticket[TICKET_COUNT]; // Tourists per ticket type

    // For logging/debugging
    int lower_station_count;        // Current tourists in lower station
    int tourists_on_chairs;         // Current tourists on chairlift

    // Config values (read-only after init)
    int station_capacity;           // Max tourists in lower station
    int tourists_to_generate;       // Total number of tourists to generate
    int tourist_spawn_delay_us;     // Delay between spawns in microseconds (0 = no delay)
    int vip_percentage;             // VIP percentage (0-100)
    int walker_percentage;          // Walker percentage (0-100)

    // Trail times in simulated minutes
    int trail_walk_time;
    int trail_bike_fast_time;
    int trail_bike_medium_time;
    int trail_bike_slow_time;

    // Ticket durations in simulated minutes
    int ticket_t1_duration;
    int ticket_t2_duration;
    int ticket_t3_duration;

    // Danger detection settings
    int danger_probability;         // Probability per check (0-100), 0 = disabled
    int danger_cooldown_sim;        // Simulated minutes between possible detections

    // Logging settings
    int debug_logs_enabled;         // 1 = show debug logs, 0 = hide debug logs

    // Process IDs for signal handling
    pid_t main_pid;
    pid_t cashier_pid;
    pid_t lower_worker_pid;
    pid_t upper_worker_pid;
    pid_t generator_pid;

    // Per-tourist tracking (flexible array - MUST BE LAST)
    int max_tracked_tourists;       // Config value for array sizing
    int tourist_entry_count;        // Number of entries used
    TouristEntry tourist_entries[]; // Flexible array member
} SharedState;

// ============================================================================
// Message Structures
// ============================================================================

// Message for cashier requests/responses
typedef struct {
    long mtype;                     // Message type (tourist_id for response)
    int tourist_id;
    TouristType tourist_type;       // Walker/cyclist
    int age;
    int is_vip;
    int ticket_type;                // TicketType (set by cashier in response)
    int ticket_valid_until;         // Sim minutes (for time-based tickets)
    int kid_count;                  // Number of kids (0-2) for family tickets
} CashierMsg;

// Message for platform/boarding communication
typedef struct {
    long mtype;                     // 1=VIP/requeued, 2=regular (for platform)
                                    // tourist_id (for boarding confirmation)
    int tourist_id;
    TouristType tourist_type;       // Walker/cyclist
    int slots_needed;               // 1 for walker, 2 for cyclist (includes kids for families)
    int kid_count;                  // Number of kids in family group (0-2)
    time_t departure_time;          // Real timestamp when chair departed (in boarding confirmation)
    int chair_id;                   // Which chair this tourist is on (for tracking)
    int tourists_on_chair;          // Total tourists on this chair
} PlatformMsg;

// Message for arrival notification
typedef struct {
    long mtype;                     // Always 1
    int tourist_id;
    int kid_count;                  // Number of kids arriving with parent (for logging)
    int chair_id;                   // Which chair arrived (for tracking)
    int tourists_on_chair;          // Total tourists expected from this chair
} ArrivalMsg;

// Message for worker-to-worker emergency communication
typedef struct {
    long mtype;                     // WORKER_DEST_LOWER or WORKER_DEST_UPPER
    int msg_type;                   // WORKER_MSG_READY_TO_RESUME or WORKER_MSG_I_AM_READY
} WorkerMsg;

// ============================================================================
// IPC Keys Structure
// ============================================================================

typedef struct {
    key_t shm_key;
    key_t sem_key;
    key_t mq_cashier_key;
    key_t mq_platform_key;
    key_t mq_boarding_key;
    key_t mq_arrivals_key;
    key_t mq_worker_key;
} IPCKeys;

// ============================================================================
// IPC IDs Structure
// ============================================================================

typedef struct {
    int shm_id;
    int sem_id;
    int mq_cashier_id;
    int mq_platform_id;
    int mq_boarding_id;
    int mq_arrivals_id;
    int mq_worker_id;
    SharedState *state;  // Attached shared memory pointer
} IPCResources;
