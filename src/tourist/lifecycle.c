/**
 * @file tourist/lifecycle.c
 * @brief Tourist ticket management and lifecycle checks.
 */

#include "tourist/lifecycle.h"
#include "ipc/messages.h"
#include "core/time_sim.h"

#include <errno.h>
#include <stdio.h>
#include <sys/msg.h>

int tourist_buy_ticket(IPCResources *res, TouristData *data) {
    CashierMsg request;
    request.mtype = MSG_CASHIER_REQUEST;
    request.tourist_id = data->id;
    request.tourist_type = data->type;
    request.age = data->age;
    request.is_vip = data->is_vip;
    request.kid_count = data->kid_count;
    request.ticket_type = data->ticket_type;

    // Send request
    if (msgsnd(res->mq_cashier_id, &request, sizeof(request) - sizeof(long), 0) == -1) {
        // Issue #6 fix: Check for EIDRM
        if (errno == EIDRM) return -1;
        perror("tourist: msgsnd cashier request");
        return -1;
    }

    // Wait for response (mtype = MSG_CASHIER_RESPONSE_BASE + tourist_id)
    CashierMsg response;
    while (1) {
        ssize_t ret = msgrcv(res->mq_cashier_id, &response,
                             sizeof(response) - sizeof(long),
                             MSG_CASHIER_RESPONSE_BASE + data->id, 0);
        if (ret == -1) {
            if (errno == EINTR) continue;
            // Issue #6 fix: Check for EIDRM
            if (errno == EIDRM) return -1;
            perror("tourist: msgrcv cashier response");
            return -1;
        }
        break;
    }

    if (response.ticket_type == -1) {
        // Rejected (station closing)
        return -1;
    }

    data->ticket_type = response.ticket_type;
    data->ticket_valid_until = response.ticket_valid_until;

    return 0;
}

int tourist_is_ticket_valid(IPCResources *res, TouristData *data) {
    // Single-use tickets are valid until used once
    if (data->ticket_type == TICKET_SINGLE) {
        return data->rides_completed == 0;
    }

    // Time-based and daily tickets: check expiration
    int current_minutes = time_get_sim_minutes(res->state);
    return current_minutes < data->ticket_valid_until;
}

int tourist_is_station_closing(IPCResources *res) {
    if (sem_wait_pauseable(res, SEM_STATE, 1) == -1) {
        return 1;  // Assume closing on failure (shutdown)
    }
    int closing = res->state->closing;
    sem_post(res->sem_id, SEM_STATE, 1);
    return closing;
}
