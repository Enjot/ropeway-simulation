#pragma once

/**
 * @file tourist/types.h
 * @brief Data structures for tourist process.
 */

#include "constants.h"
#include "ipc/ipc.h"
#include <pthread.h>

/**
 * @brief Tourist data structure containing all tourist state.
 */
typedef struct TouristData {
    int id;
    int age;
    TouristType type;
    int is_vip;
    TicketType ticket_type;
    int ticket_valid_until;  // Sim minutes
    int rides_completed;
    int station_slots;       // For lower station: 1 + kid_count (bike doesn't count)
    int chair_slots;         // For chair: walker=1, cyclist=2, plus kid_count
    int kid_count;           // Number of kids (0-2)
} TouristData;

/**
 * @brief Family state for kid/bike threads (simplified - just data, no sync primitives).
 */
typedef struct FamilyState {
    int parent_id;
    int kid_count;
    int has_bike;
    IPCResources *res;
} FamilyState;

/**
 * @brief Per-thread data for kid threads.
 */
typedef struct KidThreadData {
    int kid_index;           // 0 or 1 for kids
    FamilyState *family;
} KidThreadData;

/**
 * @brief Per-thread data for bike thread.
 */
typedef struct BikeThreadData {
    int tourist_id;
    FamilyState *family;
} BikeThreadData;
