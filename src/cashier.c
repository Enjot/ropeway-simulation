#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include "config.h"
#include "logger.h"
#include "ipc_utils.h"
#include "semaphores.h"
#include "shared_state.h"
#include "tourist.h"

static IpcResources g_ipc;
static volatile sig_atomic_t g_running = 1;

/* Signal handler */
static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_running = 0;
    }
}

/* Calculate ticket price with discounts */
static int calculate_price(TicketType ticket, int age) {
    int base_price;
    switch (ticket) {
        case TICKET_SINGLE: base_price = PRICE_SINGLE; break;
        case TICKET_TK1:    base_price = PRICE_TK1; break;
        case TICKET_TK2:    base_price = PRICE_TK2; break;
        case TICKET_TK3:    base_price = PRICE_TK3; break;
        case TICKET_DAILY:  base_price = PRICE_DAILY; break;
        default:            base_price = 0; break;
    }

    /* Apply discount for children (<10) or seniors (65+) */
    if (age < 10 || age >= SENIOR_AGE) {
        base_price = base_price * (100 - DISCOUNT_PERCENT) / 100;
    }

    return base_price;
}

/* Select ticket type based on distribution */
static TicketType select_ticket_type(void) {
    int r = rand() % 100;

    if (r < TICKET_SINGLE_PCT) {
        return TICKET_SINGLE;
    }
    r -= TICKET_SINGLE_PCT;

    if (r < TICKET_TK1_PCT) {
        return TICKET_TK1;
    }
    r -= TICKET_TK1_PCT;

    if (r < TICKET_TK2_PCT) {
        return TICKET_TK2;
    }
    r -= TICKET_TK2_PCT;

    if (r < TICKET_TK3_PCT) {
        return TICKET_TK3;
    }

    return TICKET_DAILY;
}

/* Calculate ticket expiration time */
static time_t calculate_expiration(TicketType ticket) {
    time_t now = time(NULL);

    switch (ticket) {
        case TICKET_SINGLE:
            return 0;  /* Single use - no expiration, invalidated after first ride */
        case TICKET_TK1:
            return now + TK1_DURATION_SEC;
        case TICKET_TK2:
            return now + TK2_DURATION_SEC;
        case TICKET_TK3:
            return now + TK3_DURATION_SEC;
        case TICKET_DAILY:
            return now + DAILY_DURATION_SEC;
        default:
            return 0;
    }
}

/* Process a ticket request */
static void process_request(SharedState *state) {
    CashierRequest *req = &state->cashier_request;
    CashierResponse *resp = &state->cashier_response;

    /* Select ticket type */
    TicketType ticket = select_ticket_type();

    /* Calculate price */
    int price = calculate_price(ticket, req->age);

    /* Calculate expiration */
    time_t expires = calculate_expiration(ticket);

    /* Fill response */
    resp->ticket = ticket;
    resp->price = price;
    resp->expires = expires;

    /* Update statistics */
    sem_wait_safe(g_ipc.sem_id, SEM_MUTEX);
    state->tickets_sold[ticket]++;
    state->total_revenue += price;
    sem_signal_safe(g_ipc.sem_id, SEM_MUTEX);

    LOG_INFO("Sold %s ticket to tourist %d (age %d, %s%s) for %d PLN",
             ticket_name(ticket), req->tourist_id, req->age,
             tourist_type_name(req->type),
             req->is_vip ? ", VIP" : "",
             price);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    LOG_INFO("Cashier process starting (pid=%d)", getpid());

    /* Setup signal handler */
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    /* Seed random */
    srand(time(NULL) ^ getpid());

    /* Attach to IPC resources */
    if (ipc_attach(&g_ipc) == -1) {
        LOG_ERROR("Failed to attach to IPC resources");
        return 1;
    }

    /* Signal that we're ready */
    sem_signal_safe(g_ipc.sem_id, SEM_CASHIER_READY);
    LOG_INFO("Cashier ready and waiting for customers");

    /* Main loop - process ticket requests */
    while (g_running) {
        /* Wait for a request */
        if (sem_wait_safe(g_ipc.sem_id, SEM_CASHIER_REQUEST) == -1) {
            if (!g_running) break;
            continue;
        }

        /* Check if we should stop */
        if (!g_running || g_ipc.state->ropeway_state == STATE_STOPPED) {
            break;
        }

        /* Process the request */
        process_request(g_ipc.state);

        /* Signal that response is ready */
        g_ipc.state->cashier_response_ready = 1;
        sem_signal_safe(g_ipc.sem_id, SEM_CASHIER_RESPONSE);
    }

    /* Cleanup */
    ipc_detach(&g_ipc);
    LOG_INFO("Cashier process exiting");

    return 0;
}
