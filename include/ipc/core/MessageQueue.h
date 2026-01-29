#pragma once

#include <sys/ipc.h>
#include <sys/msg.h>
#include <cerrno>
#include <cstdio>
#include <optional>

#include "logging/Logger.h"
#include "IpcException.h"

template<typename T>
class MessageQueue {
public:
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

    // Convenience constructor for child processes
    MessageQueue(const key_t key, bool /*unused*/) : MessageQueue(key, "MessageQueue") {
    }

    ~MessageQueue() = default;

    MessageQueue(const MessageQueue &) = delete;

    MessageQueue &operator=(const MessageQueue &) = delete;

    // Send with explicit type
    void send(const T &message, const long type, const int32_t flags = 0) const {
        Wrapper wrapper{};
        wrapper.mtype = type;
        wrapper.message = message;
        if (msgsnd(msgId_, &wrapper, sizeof(T), flags) == -1) {
            Logger::pError("Failed to send message to queue");
            throw ipc_exception("Failed to send message");
        }
    }

    // Convenience send that returns bool (for process code compatibility).
    // Retries on EINTR (signal interruption) to avoid silent send failures.
    bool send(const T &message, const long type) {
        Wrapper wrapper{};
        wrapper.mtype = type;
        wrapper.message = message;
        while (msgsnd(msgId_, &wrapper, sizeof(T), 0) == -1) {
            if (errno == EINTR) continue; // Retry on signal interruption
            return false; // Real error
        }
        return true;
    }

    // Non-blocking send - returns false if queue is full (IPC_NOWAIT)
    bool trySend(const T &message, const long type) {
        Wrapper wrapper{};
        wrapper.mtype = type;
        wrapper.message = message;
        return msgsnd(msgId_, &wrapper, sizeof(T), IPC_NOWAIT) != -1;
    }

    // Blocking receive - returns nullopt on EINTR (caller should check exit flag)
    std::optional<T> receive(const long type, const int32_t flags = 0) {
        Wrapper wrapper{};
        if (msgrcv(msgId_, &wrapper, sizeof(T), type, flags) != -1) {
            return wrapper.message;
        }
        return std::nullopt; // EINTR or error - caller checks signals
    }

    // Non-blocking receive
    std::optional<T> tryReceive(const long type) {
        return receive(type, IPC_NOWAIT);
    }

    // Blocking receive that returns on signal interrupt (for signal-driven loops)
    std::optional<T> receiveInterruptible(const long type) {
        Wrapper wrapper{};
        if (msgrcv(msgId_, &wrapper, sizeof(T), type, 0) != -1) {
            return wrapper.message;
        }
        return std::nullopt; // EINTR or error - caller checks signals
    }

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
