#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include "config.h"
#include "tourist.h"

/* Ropeway operational states */
typedef enum {
    STATE_STOPPED = 0,
    STATE_RUNNING,
    STATE_CLOSING,
    STATE_EMERGENCY
} RopewayState;

/* Cashier request structure */
typedef struct {
    int tourist_id;
    int age;
    TouristType type;
    int is_vip;
} CashierRequest;

/* Cashier response structure */
typedef struct {
    TicketType ticket;
    int price;
    time_t expires;         /* 0 for single-use */
} CashierResponse;

/* Main shared state structure */
typedef struct {
    /* Operational state */
    volatile RopewayState ropeway_state;
    volatile int tourists_in_station;
    volatile int tourists_on_chairs;
    volatile int total_rides;
    volatile int total_revenue;

    /* Process IDs */
    pid_t main_pid;
    pid_t cashier_pid;

    /* Time tracking */
    time_t simulation_start;
    time_t closing_time;

    /* Tourist ID counter */
    volatile int next_tourist_id;

    /* Cashier communication */
    CashierRequest cashier_request;
    CashierResponse cashier_response;
    volatile int cashier_request_ready;
    volatile int cashier_response_ready;

    /* Chair slots (simplified for MVP) */
    volatile int chairs_available_slots;  /* Total available slots */

    /* Tourist records for final report */
    TouristRecord tourist_records[MAX_TOURISTS];
    int tourist_count;

    /* Statistics */
    int tickets_sold[6];    /* Count per ticket type */
    int pedestrian_rides;
    int cyclist_rides;

} SharedState;

/* Get state name for logging */
static inline const char* state_name(RopewayState s) {
    switch(s) {
        case STATE_STOPPED:   return "STOPPED";
        case STATE_RUNNING:   return "RUNNING";
        case STATE_CLOSING:   return "CLOSING";
        case STATE_EMERGENCY: return "EMERGENCY";
        default:              return "UNKNOWN";
    }
}

#endif /* SHARED_STATE_H */
