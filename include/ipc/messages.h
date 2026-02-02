#pragma once

/**
 * @file ipc/messages.h
 * @brief Message queue structures for inter-process communication.
 */

#include "constants.h"

// ============================================================================
// Message Structures
// ============================================================================

/**
 * @brief Message for cashier requests/responses.
 */
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

/**
 * @brief Message for platform/boarding communication.
 */
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

/**
 * @brief Message for arrival notification.
 */
typedef struct {
    long mtype;                     // Always 1
    int tourist_id;
    TouristType tourist_type;       // Walker/cyclist (for logging tag determination)
    int kid_count;                  // Number of kids arriving with parent (for logging)
    int chair_id;                   // Which chair arrived (for tracking)
    int tourists_on_chair;          // Total tourists expected from this chair
} ArrivalMsg;

/**
 * @brief Message for worker-to-worker emergency communication.
 */
typedef struct {
    long mtype;                     // WORKER_DEST_LOWER or WORKER_DEST_UPPER
    int msg_type;                   // WORKER_MSG_READY_TO_RESUME or WORKER_MSG_I_AM_READY
} WorkerMsg;
