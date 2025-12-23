#pragma once

#include <cstdint>
#include <ctime>
#include "common/worker_signal.hpp"

/**
 * Structure for worker-to-worker communication
 * Used with message queues
 */
struct WorkerMessage {
    uint64_t messageType;
    uint32_t senderId;
    uint32_t receiverId;
    WorkerSignal signal;
    time_t timestamp;
    char messageText[256];

    WorkerMessage() : messageType{1},
                      senderId{0},
                      receiverId{0},
                      signal{WorkerSignal::STATION_CLEAR},
                      timestamp{0},
                      messageText{""} {
    }
};
