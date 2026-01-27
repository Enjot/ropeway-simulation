#pragma once

#include <sys/ipc.h>
#include <sys/msg.h>
#include <cerrno>
#include <optional>

#include "logging/Logger.hpp"
#include "IpcException.hpp"

template<typename T>
class MessageQueue {
public:
    explicit MessageQueue(const key_t key, const char *tag) : tag_{tag} {
        msgId_ = msgget(key, IPC_CREAT | IPC_EXCL | permissions);
        if (msgId_ == -1) {
            if (errno == EEXIST) {
                msgId_ = msgget(key, permissions);
                if (msgId_ == -1) {
                    throw ipc_exception("Failed to connect to existing message queue");
                }
                Logger::debug(tag_, "Message queue connected");
            } else {
                throw ipc_exception("Failed to create message queue");
            }
        } else {
            Logger::debug(tag_, "Message queue created");
        }
    }

    // Convenience constructor for child processes
    MessageQueue(const key_t key, bool /*unused*/) : MessageQueue(key, "MessageQueue") {}

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

    // Convenience send that returns bool (for process code compatibility)
    bool send(const T &message, const long type) {
        Wrapper wrapper{};
        wrapper.mtype = type;
        wrapper.message = message;
        return msgsnd(msgId_, &wrapper, sizeof(T), 0) != -1;
    }

    // Blocking receive (retries on EINTR)
    std::optional<T> receive(const long type, const int32_t flags = 0) {
        Wrapper wrapper{};
        while (true) {
            if (msgrcv(msgId_, &wrapper, sizeof(T), type, flags) != -1) {
                return wrapper.message;
            }
            // Retry if interrupted by signal
            if (errno == EINTR) {
                continue;
            }
            return std::nullopt;
        }
    }

    // Non-blocking receive
    std::optional<T> tryReceive(const long type) {
        return receive(type, IPC_NOWAIT);
    }

    void destroy() const {
        if (msgctl(msgId_, IPC_RMID, nullptr) == -1) {
            throw ipc_exception("Failed to destroy message queue");
        }
        Logger::debug(tag_, "destroyed");
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
