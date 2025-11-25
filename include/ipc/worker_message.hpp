#pragma once

#include <ctime>
#include "common/enums.hpp"

/**
 * Structure for worker-to-worker communication
 * Used with message queues
 */
struct WorkerMessage {
    long messageType;            // Message type for message queue
    int senderId;                // Worker ID sending message (1 or 2)
    int receiverId;              // Worker ID receiving message (1 or 2)
    WorkerSignal signal;         // Signal type
    time_t timestamp;            // Message timestamp
    char messageText[256];       // Additional message text

    WorkerMessage() : messageType(1), senderId(0), receiverId(0), signal(WorkerSignal::STATION_CLEAR), timestamp(0) {
        messageText[0] = '\0';
    }
};
