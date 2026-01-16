#pragma once

#include <sys/ipc.h>
#include <sys/msg.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <optional>

/**
 * RAII wrapper for System V message queue
 * Template class to support different message structure types
 *
 * Note: Message structures must have 'long mtype' as their first member
 */
template<typename T>
class MessageQueue {
public:
    /**
     * Create or attach to message queue
     * @param key IPC key for message queue
     * @param create If true, creates new queue; if false, attaches to existing
     */
    explicit MessageQueue(key_t key, bool create = true)
        : key_{key}, msgId_{-1}, isOwner_{create} {

        if (create) {
            msgId_ = msgget(key_, IPC_CREAT | IPC_EXCL | 0600);
            if (msgId_ == -1) {
                perror("msgget (create)");
                throw std::runtime_error("Failed to create message queue: " +
                    std::string(strerror(errno)));
            }
        } else {
            msgId_ = msgget(key_, 0600);
            if (msgId_ == -1) {
                perror("msgget (attach)");
                throw std::runtime_error("Failed to get message queue: " +
                    std::string(strerror(errno)));
            }
        }
    }

    ~MessageQueue() {
        if (isOwner_ && msgId_ != -1) {
            if (msgctl(msgId_, IPC_RMID, nullptr) == -1) {
                perror("msgctl IPC_RMID");
            }
        }
    }

    MessageQueue(const MessageQueue&) = delete;
    MessageQueue& operator=(const MessageQueue&) = delete;

    MessageQueue(MessageQueue&& other) noexcept
        : key_{other.key_}, msgId_{other.msgId_}, isOwner_{other.isOwner_} {
        other.msgId_ = -1;
        other.isOwner_ = false;
    }

    MessageQueue& operator=(MessageQueue&& other) noexcept {
        if (this != &other) {
            if (isOwner_ && msgId_ != -1) {
                msgctl(msgId_, IPC_RMID, nullptr);
            }

            key_ = other.key_;
            msgId_ = other.msgId_;
            isOwner_ = other.isOwner_;

            other.msgId_ = -1;
            other.isOwner_ = false;
        }
        return *this;
    }

    /**
     * Send a message to the queue
     * @param message The message to send (must have mtype set)
     * @param flags Optional flags (e.g., IPC_NOWAIT)
     * @return true on success, false on error
     */
    bool send(const T& message, int flags = 0) {
        if (msgsnd(msgId_, &message, sizeof(T) - sizeof(long), flags) == -1) {
            if (errno == EINTR) {
                // Signal interrupted - return false to let caller check signals
                return false;
            }
            if (errno == EAGAIN && (flags & IPC_NOWAIT)) {
                return false;
            }
            perror("msgsnd");
            return false;
        }
        return true;
    }

    /**
     * Receive a message from the queue (blocking)
     * @param msgType Message type to receive (0 = any, >0 = specific type, <0 = lowest type <= |msgType|)
     * @param flags Optional flags (e.g., IPC_NOWAIT, MSG_NOERROR)
     * @return The received message, or nullopt on error
     */
    std::optional<T> receive(long msgType = 0, int flags = 0) {
        T message{};
        ssize_t result = msgrcv(msgId_, &message, sizeof(T) - sizeof(long), msgType, flags);
        if (result == -1) {
            if (errno == EINTR) {
                // Signal interrupted - return nullopt to let caller check signals
                return std::nullopt;
            }
            if (errno == ENOMSG && (flags & IPC_NOWAIT)) {
                return std::nullopt;
            }
            perror("msgrcv");
            return std::nullopt;
        }
        return message;
    }

    /**
     * Try to receive a message (non-blocking)
     * @param msgType Message type to receive
     * @return The received message, or nullopt if no message available
     */
    std::optional<T> tryReceive(long msgType = 0) {
        return receive(msgType, IPC_NOWAIT);
    }

    /**
     * Check if there are messages waiting in the queue
     */
    [[nodiscard]] bool hasMessages() const {
        struct msqid_ds info{};
        if (msgctl(msgId_, IPC_STAT, &info) == -1) {
            perror("msgctl IPC_STAT");
            return false;
        }
        return info.msg_qnum > 0;
    }

    /**
     * Get the number of messages in the queue
     */
    [[nodiscard]] uint64_t getMessageCount() const {
        struct msqid_ds info{};
        if (msgctl(msgId_, IPC_STAT, &info) == -1) {
            perror("msgctl IPC_STAT");
            return 0;
        }
        return info.msg_qnum;
    }

    [[nodiscard]] int getId() const noexcept { return msgId_; }
    [[nodiscard]] key_t getKey() const noexcept { return key_; }
    [[nodiscard]] bool isOwner() const noexcept { return isOwner_; }

    /**
     * Release ownership (queue won't be removed on destruction)
     */
    void releaseOwnership() noexcept {
        isOwner_ = false;
    }

    /**
     * Remove the message queue
     */
    bool remove() {
        if (msgId_ != -1) {
            if (msgctl(msgId_, IPC_RMID, nullptr) == -1) {
                perror("msgctl IPC_RMID (remove)");
                return false;
            }
            isOwner_ = false;
            return true;
        }
        return false;
    }

    /**
     * Check if message queue exists for given key
     */
    static bool exists(key_t key) {
        int id = msgget(key, 0);
        return id != -1;
    }

    /**
     * Remove existing message queue by key (cleanup utility)
     */
    static bool removeByKey(key_t key) {
        int id = msgget(key, 0);
        if (id == -1) {
            return false;
        }
        if (msgctl(id, IPC_RMID, nullptr) == -1) {
            perror("msgctl IPC_RMID (removeByKey)");
            return false;
        }
        return true;
    }

private:
    key_t key_;
    int msgId_;
    bool isOwner_;
};
