#ifndef TOURIST_H
#define TOURIST_H

#include <stdint.h>
#include <time.h>

/* Ticket types */
typedef enum {
    TICKET_NONE = 0,
    TICKET_SINGLE,      /* Single use */
    TICKET_TK1,         /* Time-based 1 hour */
    TICKET_TK2,         /* Time-based 2 hours */
    TICKET_TK3,         /* Time-based 4 hours */
    TICKET_DAILY        /* Full day */
} TicketType;

/* Tourist type */
typedef enum {
    TOURIST_PEDESTRIAN = 0,
    TOURIST_CYCLIST = 1
} TouristType;

/* Trail difficulty for cyclists */
typedef enum {
    TRAIL_EASY = 0,     /* T1 - shortest */
    TRAIL_MEDIUM = 1,   /* T2 - medium */
    TRAIL_HARD = 2,     /* T3 - longest */
    TRAIL_WALKING = 3   /* For pedestrians */
} TrailType;

/* Tourist record for tracking rides */
typedef struct {
    int id;
    int age;
    TouristType type;
    TicketType ticket;
    int is_vip;
    int rides_completed;
    int price_paid;
    time_t ticket_expires;  /* 0 for single-use */
} TouristRecord;

/* Tourist state in shared memory */
typedef struct {
    int id;
    int age;
    TouristType type;
    TicketType ticket;
    int is_vip;
    int rides_completed;
    time_t ticket_expires;
    int active;             /* 1 if tourist still in simulation */
} Tourist;

/* Calculate slots needed for a tourist */
static inline int tourist_slots(TouristType type) {
    return (type == TOURIST_CYCLIST) ? 2 : 1;
}

/* Get ticket name for logging */
static inline const char* ticket_name(TicketType t) {
    switch(t) {
        case TICKET_SINGLE: return "SINGLE";
        case TICKET_TK1:    return "TK1";
        case TICKET_TK2:    return "TK2";
        case TICKET_TK3:    return "TK3";
        case TICKET_DAILY:  return "DAILY";
        default:            return "NONE";
    }
}

/* Get tourist type name */
static inline const char* tourist_type_name(TouristType t) {
    return (t == TOURIST_CYCLIST) ? "cyclist" : "pedestrian";
}

#endif /* TOURIST_H */
