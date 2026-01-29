#pragma once

#include <sys/ipc.h>
#include <sys/msg.h>
#include <cerrno>
#include <cstdio>
#include <optional>

#include "logging/Logger.h"
#include "IpcException.h"

/**
 * @brief RAII wrapper for System V message queues.
 * @tparam T Type of message payload
 *
 * Provides type-safe sending and receiving of messages with automatic
 * EINTR handling for signal safety.
 */
template<typename T>
class MessageQueue {
public:
    /**
     * @brief Create or connect to a message queue.
     * @param key System V IPC key
     * @param tag Identifier for logging
     *
     * Creates queue if it doesn't exist, otherwise connects to existing.
     */
    explicit MessageQueue(const key_t key, const char *tag) : tag_{tag} {
        msgId_ = msgget(key, IPC_CREAT | IPC_EXCL | permissions);
        if (msgId_ == -1) {
            if (errno == EEXIST) {
                msgId_ = msgget(key, permissions);
                if (msgId_ == -1) {
                    perror("msgget (connect)");
                    throw ipc_exception("Failed to connect to existing message queue");
                }
                Logger::debug(Logger::Source::Other, tag_, "Message queue connected");
            } else {
                perror("msgget (create)");
                throw ipc_exception("Failed to create message queue");
            }
        } else {
            Logger::debug(Logger::Source::Other, tag_, "Message queue created");
        }
    }

    /**
     * @brief Convenience constructor for child processes.
     * @param key System V IPC key
     * @param unused Unused parameter (for overload disambiguation)
     */
    MessageQueue(const key_t key, bool /*unused*/) : MessageQueue(key, "MessageQueue") {
    }

    ~MessageQueue() = default;

    MessageQueue(const MessageQueue &) = delete;

    MessageQueue &operator=(const MessageQueue &) = delete;

    /**
     * @brief Send a message (blocking).
     * @param message Message payload to send
     * @param type Message type for priority/filtering
     * @throws ipc_exception If sending fails
     *
     * Handles EINTR for signal safety. Blocks if queue is full.
     */
    void send(const T &message, const long type) const {
        Wrapper wrapper{};
        wrapper.mtype = type;
        wrapper.message = message;
        while (msgsnd(msgId_, &wrapper, sizeof(T), 0) == -1) {
            if (errno == EINTR) continue;
            Logger::pError("Failed to send message to queue");
            throw ipc_exception("Failed to send message");
        }
    }

    /**
     * @brief Try to send a message (non-blocking).
     * @param message Message payload to send
     * @param type Message type for priority/filtering
     * @return true if sent successfully, false if queue is full
     */
    bool trySend(const T &message, const long type) {
        Wrapper wrapper{};
        wrapper.mtype = type;
        wrapper.message = message;
        return msgsnd(msgId_, &wrapper, sizeof(T), IPC_NOWAIT) != -1;
    }

    /**
     * @brief Receive a message (blocking).
     * @param type Message type to receive (0 = any, >0 = exact, <0 = priority)
     * @param flags Additional msgrcv flags
     * @return Message if received, nullopt on EINTR or error
     *
     * Caller should check exit signals when nullopt is returned.
     */
    std::optional<T> receive(const long type, const int32_t flags = 0) {
        Wrapper wrapper{};
        if (msgrcv(msgId_, &wrapper, sizeof(T), type, flags) != -1) {
            return wrapper.message;
        }
        return std::nullopt; // EINTR or error - caller checks signals
    }

    /**
     * @brief Try to receive a message (non-blocking).
     * @param type Message type to receive
     * @return Message if available, nullopt if queue is empty
     */
    std::optional<T> tryReceive(const long type) {
        return receive(type, IPC_NOWAIT);
    }

    /**
     * @brief Receive a message, returning on signal interrupt.
     * @param type Message type to receive
     * @return Message if received, nullopt on EINTR
     *
     * Designed for signal-driven loops that need to check flags after interruption.
     */
    std::optional<T> receiveInterruptible(const long type) {
        Wrapper wrapper{};
        if (msgrcv(msgId_, &wrapper, sizeof(T), type, 0) != -1) {
            return wrapper.message;
        }
        return std::nullopt; // EINTR or error - caller checks signals
    }

    /**
     * @brief Destroy the message queue.
     * @throws ipc_exception If destruction fails
     */
    void destroy() const {
        if (msgctl(msgId_, IPC_RMID, nullptr) == -1) {
            perror("msgctl IPC_RMID");
            throw ipc_exception("Failed to destroy message queue");
        }
        Logger::debug(Logger::Source::Other, tag_, "destroyed");
    }

private:
    const char *tag_;
    int msgId_;
    static constexpr int32_t permissions = 0600;

    struct Wrapper {
        long mtype;
        T message;
    };
};
