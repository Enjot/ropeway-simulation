#pragma once

#include <cstdint>
#include <ctime>
#include <cstring>
#include "ropeway/worker/WorkerSignal.h"

/**
 * Structure for worker-to-worker communication
 * Used with System V message queues
 * Note: mtype must be the first member (long type) for msgsnd/msgrcv
 */
struct WorkerMessage {
    uint32_t senderId;
    uint32_t receiverId;
    WorkerSignal signal;
    time_t timestamp;
    char messageText[256];

    WorkerMessage() : senderId{0},
                      receiverId{0},
                      signal{WorkerSignal::STATION_CLEAR},
                      timestamp{0},
                      messageText{} {
        std::memset(messageText, 0, sizeof(messageText));
    }
};
