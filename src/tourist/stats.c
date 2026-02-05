/**
 * @file tourist/stats.c
 * @brief Statistics recording for final report.
 */

#include "tourist/stats.h"
#include "core/time_sim.h"

/**
 * @brief Record tourist entry in shared state for final report.
 *
 * @param res IPC resources.
 * @param data Tourist data.
 */
void tourist_record_entry(IPCResources *res, TouristData *data) {
    if (sem_wait_pauseable(res, SEM_STATS, 1) == -1) {
        return;  // Shutdown in progress
    }

    int idx = data->id - 1;
    if (idx >= 0 && idx < res->state->max_tracked_tourists) {
        TouristEntry *entry = &res->state->tourist_entries[idx];
        entry->active = 1;
        entry->tourist_id = data->id;
        entry->ticket_type = data->ticket_type;
        entry->entry_time_sim = time_get_sim_minutes(res->state);
        entry->total_rides = 0;
        entry->is_vip = data->is_vip;
        entry->tourist_type = data->type;
        entry->kid_count = data->kid_count;

        if (idx >= res->state->tourist_entry_count) {
            res->state->tourist_entry_count = idx + 1;
        }
    }

    sem_post(res->sem_id, SEM_STATS, 1);
}

/**
 * @brief Update ride statistics after completing a ride.
 *
 * @param res IPC resources.
 * @param data Tourist data.
 */
void tourist_update_stats(IPCResources *res, TouristData *data) {
    if (sem_wait_pauseable(res, SEM_STATS, 1) == -1) {
        return;  // Shutdown in progress
    }

    // Count parent ride
    res->state->total_rides++;
    if (data->ticket_type >= 0 && data->ticket_type < TICKET_COUNT) {
        res->state->rides_by_ticket[data->ticket_type]++;
    }

    // Update per-tourist ride count
    int idx = data->id - 1;
    if (idx >= 0 && idx < res->state->max_tracked_tourists) {
        res->state->tourist_entries[idx].total_rides++;
    }

    // Count kid rides (same ticket type as parent)
    for (int i = 0; i < data->kid_count; i++) {
        res->state->total_rides++;
        if (data->ticket_type >= 0 && data->ticket_type < TICKET_COUNT) {
            res->state->rides_by_ticket[data->ticket_type]++;
        }
    }

    sem_post(res->sem_id, SEM_STATS, 1);
}
