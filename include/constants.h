#pragma once

/**
 * @file constants.h
 * @brief Global constants, capacity limits, semaphore indices, message queue IDs, and enums.
 */

#include <sys/types.h>
#include <time.h>
#include <stdint.h>

// ============================================================================
// Capacity Constants
// ============================================================================

#define CHAIR_CAPACITY 4          // Max slots per chair (walkers=1, cyclists=2)
#define MAX_CHAIRS_IN_TRANSIT 36  // Maximum chairs simultaneously in use
#define TOTAL_CHAIRS 72           // Total chairs in the system
#define ENTRY_GATES 4             // Number of entry gates at lower station
#define EXIT_GATES 2              // Number of exit gates at upper station
#define PLATFORM_GATES 3          // Number of platform gates (before boarding)
#define MAX_KIDS_PER_ADULT 2      // Maximum kids per guardian

// ============================================================================
// Semaphore Indices
// ============================================================================

#define SEM_STATE 0           // Mutex for SharedState access
#define SEM_STATS 1           // Mutex for statistics access
#define SEM_ENTRY_GATES 2     // Entry gates capacity (4)
#define SEM_EXIT_GATES 3      // Exit gates capacity (2)
#define SEM_LOWER_STATION 4   // Lower station capacity (N)
#define SEM_CHAIRS 5          // Chair availability (36)
#define SEM_WORKER_READY 6    // Startup barrier: workers post when ready
#define SEM_PLATFORM_GATES 7  // Platform gates capacity (3)
#define SEM_EMERGENCY_CLEAR 8 // Released when emergency cleared (for tourist waiters)
#define SEM_COUNT 9           // Total number of semaphores

// Number of workers that must signal ready before generator starts
// (TimeServer, Cashier, LowerWorker, UpperWorker)
#define WORKER_COUNT_FOR_BARRIER 4

// ============================================================================
// Message Queue IDs (for ftok project_id)
// ============================================================================

#define MQ_CASHIER_ID 1       // Tourist <-> Cashier
#define MQ_PLATFORM_ID 2      // Tourist -> Lower Worker (ready to board)
#define MQ_BOARDING_ID 3      // Lower Worker -> Tourist (boarding confirmation)
#define MQ_ARRIVALS_ID 4      // Tourist -> Upper Worker (arrival notification)
#define MQ_WORKER_ID 5        // Worker <-> Worker emergency communication

// ============================================================================
// Worker Message Constants
// ============================================================================

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
    TOURIST_CYCLIST = 1,
    TOURIST_FAMILY = 2
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
    WORKER_LOWER = 0,     // Lower platform worker
    WORKER_UPPER = 1      // Upper platform worker
} WorkerRole;

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
