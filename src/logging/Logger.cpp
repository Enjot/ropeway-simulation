#include "logging/Logger.h"
#include "ipc/core/SharedMemory.h"
#include "ipc/core/Semaphore.h"
#include "ipc/core/MessageQueue.h"
#include "ipc/model/SharedRopewayState.h"

namespace Logger {
    namespace detail {
        void sendToQueue(Level level, const char* tag, const char* text) {
            if (!centralizedMode || logQueue == nullptr || sem == nullptr || shm == nullptr) {
                // Fallback to direct logging
                logDirect(level, tag, "%s", text);
                return;
            }

            LogMessage msg;
            msg.level = static_cast<uint8_t>(level);
            strncpy(msg.tag, tag, sizeof(msg.tag) - 1);
            msg.tag[sizeof(msg.tag) - 1] = '\0';
            strncpy(msg.text, text, sizeof(msg.text) - 1);
            msg.text[sizeof(msg.text) - 1] = '\0';
            gettimeofday(&msg.timestamp, nullptr);

            // Try to acquire queue slot (non-blocking to avoid deadlock)
            if (!sem->tryAcquire(Semaphore::Index::LOG_QUEUE_SLOTS)) {
                // Queue full - fall back to direct logging
                logDirect(level, tag, "%s", text);
                return;
            }

            // Get sequence number atomically - use as mtype for ordering
            {
                Semaphore::ScopedLock lock(*sem, Semaphore::Index::LOG_SEQUENCE);
                msg.sequenceNum = ++shm->get()->operational.logSequenceNum; // Start from 1 (mtype must be > 0)
            }

            // Send to queue with sequence number as mtype (enables ordered retrieval)
            logQueue->send(msg, static_cast<long>(msg.sequenceNum));
        }
    }

    void initCentralized(key_t shmKey, key_t semKey, key_t logQueueKey) {
        detail::shmKey = shmKey;
        detail::semKey = semKey;
        detail::logQueueKey = logQueueKey;

        try {
            detail::shm = new SharedMemory<SharedRopewayState>(
                SharedMemory<SharedRopewayState>::attach(shmKey));
            detail::sem = new Semaphore(semKey);
            detail::logQueue = new MessageQueue<LogMessage>(logQueueKey, "LogQueue");
            detail::centralizedMode = true;
        } catch (...) {
            // Fallback to direct logging if initialization fails
            cleanupCentralized();
        }
    }

    void cleanupCentralized() {
        detail::centralizedMode = false;
        delete detail::logQueue;
        detail::logQueue = nullptr;
        delete detail::sem;
        detail::sem = nullptr;
        delete detail::shm;
        detail::shm = nullptr;
    }
}
