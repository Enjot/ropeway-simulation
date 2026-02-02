/**
 * @file tourist/boarding.c
 * @brief Platform and chair IPC communication with workers.
 */

#include "tourist/boarding.h"
#include "ipc/messages.h"
#include "logger.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/msg.h>

int tourist_board_chair(IPCResources *res, TouristData *data, time_t *departure_time_out,
                        int *chair_id_out, int *tourists_on_chair_out) {
    // Note: SEM_CHAIRS is now acquired by lower_worker when chair departs,
    // and released by upper_worker when all tourists from that chair arrive.

    // Kernel handles SIGTSTP automatically

    // Issue #2, #4 fix: Check emergency stop with semaphore protection and blocking
    if (sem_wait_pauseable(res, SEM_STATE, 1) == -1) {
        return -1;
    }
    int emergency = res->state->emergency_stop;
    sem_post(res->sem_id, SEM_STATE, 1);

    if (emergency) {
        // Issue #4 fix: Block on semaphore instead of polling with usleep
        ipc_wait_emergency_clear(res);
    }

    // Send "ready to board" message to lower worker
    PlatformMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.mtype = 2;  // No VIP priority at boarding (VIPs skip entry gates instead)
    msg.tourist_id = data->id;
    msg.tourist_type = data->type;
    msg.slots_needed = data->chair_slots;  // Chair slots include bike for cyclists
    msg.kid_count = data->kid_count;

    if (msgsnd(res->mq_platform_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
        if (errno == EINTR) return -1;
        // Issue #6 fix: Check for EIDRM
        if (errno == EIDRM) {
            return -1;
        }
        perror("tourist: msgsnd platform");
        return -1;
    }

    // Wait for boarding confirmation (mtype = tourist_id)
    PlatformMsg response;
    while (1) {
        ssize_t ret = msgrcv(res->mq_boarding_id, &response,
                             sizeof(response) - sizeof(long), data->id, 0);
        if (ret == -1) {
            if (errno == EINTR) {
                // Kernel handles SIGTSTP automatically
                continue;
            }
            // Issue #6 fix: Check for EIDRM
            if (errno == EIDRM) {
                return -1;
            }
            perror("tourist: msgrcv boarding");
            return -1;
        }
        break;
    }

    // Return the departure time and chair info for synchronized arrival
    if (departure_time_out) *departure_time_out = response.departure_time;
    if (chair_id_out) *chair_id_out = response.chair_id;
    if (tourists_on_chair_out) *tourists_on_chair_out = response.tourists_on_chair;

    return 0;
}

int tourist_arrive_upper(IPCResources *res, TouristData *data,
                         int chair_id, int tourists_on_chair) {
    // Note: SEM_CHAIRS is released by upper_worker when all tourists from chair arrive

    // Wait for exit gate
    if (sem_wait_pauseable(res, SEM_EXIT_GATES, 1) == -1) {
        return -1;
    }

    // Kernel handles SIGTSTP automatically

    // Notify upper worker of arrival (with chair info for tracking)
    ArrivalMsg msg;
    msg.mtype = 1;
    msg.tourist_id = data->id;
    msg.tourist_type = data->type;    // For logging tag determination
    msg.kid_count = data->kid_count;  // For family logging at upper platform
    msg.chair_id = chair_id;
    msg.tourists_on_chair = tourists_on_chair;

    if (msgsnd(res->mq_arrivals_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
        // Issue #6 fix: Check for EIDRM (queue removed during shutdown)
        if (errno != EINTR && errno != EIDRM) {
            perror("tourist: msgsnd arrivals");
        }
    }

    // Release exit gate
    sem_post(res->sem_id, SEM_EXIT_GATES, 1);

    return 0;
}
