/**
 * @file report.c
 * @brief Final simulation report generation.
 */

#include "core/report.h"
#include "constants.h"
#include "core/time_sim.h"

#include <stdio.h>
#include <string.h>

void print_report(SharedState *state) {
    printf("\n========== SIMULATION REPORT ==========\n");

    char start_buf[8], end_buf[8];
    time_format_minutes(state->sim_start_minutes, start_buf, sizeof(start_buf));
    time_format_minutes(state->sim_end_minutes, end_buf, sizeof(end_buf));

    printf("Duration: %s - %s (simulated)\n", start_buf, end_buf);
    printf("Total tourists: %d\n", state->total_tourists);
    printf("Total rides: %d\n\n", state->total_rides);

    // Per-tourist summary
    printf("--- Per-Tourist Summary ---\n");
    printf("%-6s %-8s %-10s %-8s %-6s %s\n",
           "ID", "Type", "Ticket", "Entry", "Rides", "Notes");
    printf("----------------------------------------------\n");

    const char *ticket_names[] = {"SINGLE", "TIME_T1", "TIME_T2", "TIME_T3", "DAILY"};
    const char *type_names[] = {"Walker", "Cyclist", "Family"};

    for (int i = 0; i < state->tourist_entry_count && i < state->max_tracked_tourists; i++) {
        TouristEntry *e = &state->tourist_entries[i];
        if (!e->active) continue;

        char entry_time_buf[8];
        time_format_minutes(e->entry_time_sim, entry_time_buf, sizeof(entry_time_buf));

        char notes[32] = "";
        if (e->is_vip) strcat(notes, "VIP ");
        if (e->kid_count > 0) {
            char kid_note[16];
            snprintf(kid_note, sizeof(kid_note), "+%d kids", e->kid_count);
            strcat(notes, kid_note);
        }

        printf("%-6d %-8s %-10s %-8s %-6d %s\n",
               e->tourist_id,
               type_names[e->tourist_type],
               ticket_names[e->ticket_type],
               entry_time_buf,
               e->total_rides,
               notes);
    }

    // Aggregates by ticket type
    printf("\n--- Aggregates by Ticket Type ---\n");
    for (int i = 0; i < TICKET_COUNT; i++) {
        printf("  %-10s %5d tourists, %5d rides\n",
               ticket_names[i],
               state->tourists_by_ticket[i],
               state->rides_by_ticket[i]);
    }

    printf("\n=======================================\n");
}
