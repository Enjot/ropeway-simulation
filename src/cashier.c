#include "types.h"
#include "ipc.h"
#include "logger.h"
#include "time_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/msg.h>
#include <time.h>

static int g_running = 1;

static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_running = 0;
    }
}

// Determine ticket type for a tourist
static TicketType select_ticket_type(SharedState *state) {
    (void)state;  // Future use: could vary ticket distribution based on time
    int r = rand() % 100;

    // Distribution: 30% single, 20% T1, 20% T2, 15% T3, 15% daily
    if (r < 30) return TICKET_SINGLE;
    if (r < 50) return TICKET_TIME_T1;
    if (r < 70) return TICKET_TIME_T2;
    if (r < 85) return TICKET_TIME_T3;
    return TICKET_DAILY;
}

// Calculate ticket valid until time (for time-based tickets)
static int calculate_ticket_validity(SharedState *state, TicketType ticket) {
    int current_minutes = time_get_sim_minutes(state);

    switch (ticket) {
        case TICKET_SINGLE:
            return state->sim_end_minutes;  // Valid all day (single use)

        case TICKET_TIME_T1:
            return current_minutes + state->ticket_t1_duration;

        case TICKET_TIME_T2:
            return current_minutes + state->ticket_t2_duration;

        case TICKET_TIME_T3:
            return current_minutes + state->ticket_t3_duration;

        case TICKET_DAILY:
            return state->sim_end_minutes;

        default:
            return state->sim_end_minutes;
    }
}

/**
 * Calculate ticket price for one person.
 * Age discounts: <10 years or 65+ get 25% off.
 */
static int calculate_price(int age, TicketType ticket, int is_vip) {
    // Base prices
    int base_prices[] = {15, 30, 50, 70, 80};
    int price = base_prices[ticket];

    // VIP surcharge
    if (is_vip) {
        price += 50;
    }

    // Age discounts
    if (age < 10) {
        price = (price * 75) / 100;  // 25% discount
    } else if (age >= 65) {
        price = (price * 75) / 100;  // 25% discount
    }

    return price;
}

/**
 * Calculate total family ticket price.
 * Parent and kids all get the same ticket type.
 * Kids (4-7 years old) always get the under-10 discount.
 */
static int calculate_family_price(int parent_age, TicketType ticket, int is_vip,
                                  int kid_count) {
    // Parent ticket
    int total = calculate_price(parent_age, ticket, is_vip);

    // Kid tickets - all kids are 4-7 years old, so always get under-10 discount
    // Use age 5 as representative kid age (any value < 10 works)
    for (int i = 0; i < kid_count; i++) {
        total += calculate_price(5, ticket, 0);
    }

    return total;
}

void cashier_main(IPCResources *res, IPCKeys *keys) {
    (void)keys;

    // Initialize logger with component type
    logger_init(res->state, LOG_CASHIER);

    // Install signal handlers
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    // Seed random number generator
    srand(time(NULL) ^ getpid());

    log_info("CASHIER", "Cashier ready to serve tourists");

    while (g_running && res->state->running) {
        // Check if closing
        if (res->state->closing) {
            log_info("CASHIER", "Station closing, no more tickets");
            break;
        }

        // Wait for ticket request
        CashierMsg request;
        ssize_t ret = msgrcv(res->mq_cashier_id, &request, sizeof(request) - sizeof(long), 0, 0);

        if (ret == -1) {
            if (errno == EINTR) {
                continue;  // Interrupted by signal
            }
            if (errno == EIDRM) {
                log_debug("CASHIER", "Message queue removed, exiting");
                break;
            }
            perror("cashier: msgrcv");
            continue;
        }

        // Check again if closing
        if (res->state->closing) {
            log_info("CASHIER", "Station closing, refusing ticket for tourist %d", request.tourist_id);

            // Send rejection (mtype = tourist_id, ticket_type = -1)
            CashierMsg response = request;
            response.mtype = request.tourist_id;
            response.ticket_type = -1;

            if (msgsnd(res->mq_cashier_id, &response, sizeof(response) - sizeof(long), 0) == -1) {
                // Issue #6 fix: Check for EIDRM
                if (errno == EIDRM) break;
                if (errno != EINTR) perror("cashier: msgsnd rejection");
            }
            continue;
        }

        // Determine ticket type (same for parent and kids)
        TicketType ticket = select_ticket_type(res->state);
        int valid_until = calculate_ticket_validity(res->state, ticket);

        // Calculate price for whole family
        int price;
        if (request.kid_count > 0) {
            price = calculate_family_price(request.age, ticket, request.is_vip,
                                           request.kid_count);
        } else {
            price = calculate_price(request.age, ticket, request.is_vip);
        }

        // Update statistics (count parent + kids as separate tourists)
        if (sem_wait(res->sem_id, SEM_STATS, 1) == -1) {
            continue;  // Check loop condition on failure
        }
        res->state->total_tourists += (1 + request.kid_count);
        res->state->tourists_by_ticket[ticket] += (1 + request.kid_count);
        sem_post(res->sem_id, SEM_STATS, 1);

        // Send ticket response
        CashierMsg response = request;
        response.mtype = request.tourist_id;
        response.ticket_type = ticket;
        response.ticket_valid_until = valid_until;

        if (msgsnd(res->mq_cashier_id, &response, sizeof(response) - sizeof(long), 0) == -1) {
            if (errno == EINTR) continue;
            // Issue #6 fix: Check for EIDRM
            if (errno == EIDRM) {
                log_debug("CASHIER", "Message queue removed during send");
                break;
            }
            perror("cashier: msgsnd ticket");
            continue;
        }

        const char *ticket_names[] = {"SINGLE", "TIME_T1", "TIME_T2", "TIME_T3", "DAILY"};
        const char *type_name = request.tourist_type == TOURIST_WALKER ? "walker" : "cyclist";

        char valid_buf[8];
        time_format_minutes(valid_until, valid_buf, sizeof(valid_buf));

        if (request.kid_count > 0) {
            log_info("CASHIER", "Sold %s family ticket to tourist %d (%s, age %d%s) + %d kid(s) - valid until %s, price %d PLN",
                     ticket_names[ticket],
                     request.tourist_id,
                     type_name,
                     request.age,
                     request.is_vip ? ", VIP" : "",
                     request.kid_count,
                     valid_buf,
                     price);
        } else {
            log_info("CASHIER", "Sold %s ticket to tourist %d (%s, age %d%s) - valid until %s, price %d PLN",
                     ticket_names[ticket],
                     request.tourist_id,
                     type_name,
                     request.age,
                     request.is_vip ? ", VIP" : "",
                     valid_buf,
                     price);
        }
    }

    log_info("CASHIER", "Cashier shutting down");
}
