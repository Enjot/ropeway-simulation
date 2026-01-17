#pragma once

#include <sys/ipc.h>
#include <sys/msg.h>
#include <cerrno>
#include <optional>

#include "utils/Logger.hpp"

template<typename T>
class MessageQueue {
public:
    explicit MessageQueue(const key_t key) {
        msgId_ = msgget(key | IPC_CREAT | IPC_EXCL, permissions);
        if (msgId_ == -1) {
            if (errno == EEXIST) {
                msgId_ = msgget(key, permissions);
                if (msgId_ == -1) {
                    throw ipc_exception("Failed to connect to existing message queue");
                }
                Logger::debug("Message queue connected");
            } else {
                throw ipc_exception("Failed to create message queue");
            }
        } else {
            Logger::debug("Message queue created");
        }
    }

    ~MessageQueue() = default;

    MessageQueue(const MessageQueue &) = delete;

    MessageQueue &operator=(const MessageQueue &) = delete;

    void send(const T &message, const long type, const int32_t flags = 0) const {
        Wrapper wrapper{};
        wrapper.mtype = type;
        wrapper.message = message;
        if (msgsnd(msgId_, &wrapper, sizeof(T), flags) == -1) {
            throw ipc_exception("Failed to send message");
        }
    }

    std::optional<T> receive(const long type, const int32_t flags = 0) {
        Wrapper wrapper{};
        if (msgrcv(msgId_, &wrapper, sizeof(T), type, flags) == -1) {
            return std::nullopt;
        }
        return wrapper.message;
    }

    void destroy() const {
        if (msgctl(msgId_, IPC_RMID, nullptr) == -1) {
            throw ipc_exception("Failed to destroy message queue");
        }
        Logger::debug("Message queue destroyed");
    }

private:
    int msgId_;
    static constexpr int32_t permissions = 0600;

    struct Wrapper {
        long mtype;
        T message;
    };
};
