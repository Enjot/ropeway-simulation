#ifndef CONFIG_H
#define CONFIG_H

#include <stdlib.h>

/* Helper to read env var with default */
static inline int get_env_int(const char *name, int default_val) {
    const char *val = getenv(name);
    return val ? atoi(val) : default_val;
}

/* Simulation parameters */
#define NUM_TOURISTS        get_env_int("ROPEWAY_NUM_TOURISTS", 100)
#define STATION_CAPACITY    get_env_int("ROPEWAY_STATION_CAPACITY", 50)
#define OPENING_HOUR        get_env_int("ROPEWAY_OPENING_HOUR", 8)
#define CLOSING_HOUR        get_env_int("ROPEWAY_CLOSING_HOUR", 18)
#define TIME_SCALE          get_env_int("ROPEWAY_TIME_SCALE", 600)

/* Timing (microseconds) */
#define RIDE_DURATION_US    get_env_int("ROPEWAY_RIDE_DURATION_US", 1000)
#define TRAIL_EASY_US       get_env_int("ROPEWAY_TRAIL_EASY_US", 2500)
#define TRAIL_MEDIUM_US     get_env_int("ROPEWAY_TRAIL_MEDIUM_US", 5000)
#define TRAIL_HARD_US       get_env_int("ROPEWAY_TRAIL_HARD_US", 7500)
#define ARRIVAL_DELAY_BASE  get_env_int("ROPEWAY_ARRIVAL_DELAY_BASE_US", 1000)
#define ARRIVAL_DELAY_RAND  get_env_int("ROPEWAY_ARRIVAL_DELAY_RANDOM_US", 500)

/* Ticket durations (seconds, converted to simulated time) */
#define TK1_DURATION_SEC    get_env_int("ROPEWAY_TK1_DURATION_SEC", 6)
#define TK2_DURATION_SEC    get_env_int("ROPEWAY_TK2_DURATION_SEC", 12)
#define TK3_DURATION_SEC    get_env_int("ROPEWAY_TK3_DURATION_SEC", 24)
#define DAILY_DURATION_SEC  get_env_int("ROPEWAY_DAILY_DURATION_SEC", 60)

/* Ticket distribution (percentages) */
#define TICKET_SINGLE_PCT   get_env_int("ROPEWAY_TICKET_SINGLE_USE_PCT", 40)
#define TICKET_TK1_PCT      get_env_int("ROPEWAY_TICKET_TK1_PCT", 20)
#define TICKET_TK2_PCT      get_env_int("ROPEWAY_TICKET_TK2_PCT", 15)
#define TICKET_TK3_PCT      get_env_int("ROPEWAY_TICKET_TK3_PCT", 15)
/* Daily = remaining */

/* Test flags */
#define ALL_TOURISTS_RIDE   get_env_int("ROPEWAY_ALL_TOURISTS_RIDE", 0)

/* Chair configuration */
#define NUM_CHAIRS          72
#define MAX_CHAIRS_IN_USE   36
#define SLOTS_PER_CHAIR     4
#define CYCLIST_SLOT_COST   2
#define PEDESTRIAN_SLOT_COST 1

/* Age rules */
#define MIN_ADULT_AGE       8      /* Minimum age to ride alone */
#define PARENT_MIN_AGE      26     /* Min age to bring children */
#define CHILD_MIN_AGE       4      /* Min age for supervised child */
#define CHILD_MAX_AGE       7      /* Max age for supervised child */
#define SENIOR_AGE          65     /* Senior discount threshold */
#define MAX_CHILDREN        2      /* Max children per adult */

/* Gate configuration */
#define NUM_ENTRY_GATES     4
#define NUM_RIDE_GATES      3
#define NUM_EXIT_GATES      2

/* Capacities */
#define MAX_TOURISTS        6000   /* Max tracked tourists */
#define EXIT_CAPACITY       4      /* Concurrent exits allowed */

/* Prices (PLN) */
#define PRICE_SINGLE        15
#define PRICE_TK1           25
#define PRICE_TK2           40
#define PRICE_TK3           60
#define PRICE_DAILY         80
#define DISCOUNT_PERCENT    25     /* Discount for children/seniors */

/* VIP probability (1%) */
#define VIP_PERCENT         1

/* IPC keys - using ftok with project IDs */
#define IPC_PATH            "/tmp"
#define SHM_PROJ_ID         'S'
#define SEM_PROJ_ID         'M'

#endif /* CONFIG_H */
