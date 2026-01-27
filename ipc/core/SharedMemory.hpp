#pragma once

#include <sys/ipc.h>
#include <sys/shm.h>
#include <cerrno>

#include "IpcException.hpp"
#include "logging/Logger.hpp"

template<typename T>
class SharedMemory {
public:
    // Convenience constructor for child processes (always attaches, never creates)
    SharedMemory(const key_t key, bool /*unused*/)
        : SharedMemory(attach(key)) {}

    static SharedMemory create(const key_t key) {
        int id = shmget(key, sizeof(T), IPC_CREAT | IPC_EXCL | kPermissions);
        if (id == -1 && errno == EEXIST) {
            shmctl(shmget(key, 0, 0), IPC_RMID, nullptr);
            id = shmget(key, sizeof(T), IPC_CREAT | IPC_EXCL | kPermissions);
        }
        if (id == -1) {
            throw ipc_exception("shmget create failed");
        }
        return SharedMemory(key, id, true);
    }

    static SharedMemory attach(const key_t key) {
        const int id = shmget(key, 0, 0);
        if (id == -1) {
            throw ipc_exception("shmget attach failed");
        }
        return SharedMemory(key, id, false);
    }

    ~SharedMemory() {
        if (data_ != nullptr) {
            shmdt(data_);
        }
        if (isOwner_ && shmId_ != -1) {
            shmctl(shmId_, IPC_RMID, nullptr);
        }
    }

    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    SharedMemory(SharedMemory&& other) noexcept
        : key_{other.key_}, shmId_{other.shmId_},
          data_{other.data_}, isOwner_{other.isOwner_} {
        other.data_ = nullptr;
        other.isOwner_ = false;
    }

    SharedMemory& operator=(SharedMemory&& other) noexcept {
        if (this != &other) {
            if (data_ != nullptr) shmdt(data_);
            if (isOwner_ && shmId_ != -1) shmctl(shmId_, IPC_RMID, nullptr);

            key_ = other.key_;
            shmId_ = other.shmId_;
            data_ = other.data_;
            isOwner_ = other.isOwner_;

            other.data_ = nullptr;
            other.isOwner_ = false;
        }
        return *this;
    }

    T* get() noexcept { return data_; }
    const T* get() const noexcept { return data_; }

    T* operator->() noexcept { return data_; }
    const T* operator->() const noexcept { return data_; }

    T& operator*() noexcept { return *data_; }
    const T& operator*() const noexcept { return *data_; }

    int getId() const noexcept { return shmId_; }
    key_t getKey() const noexcept { return key_; }
    bool isOwner() const noexcept { return isOwner_; }

    static bool exists(const key_t key) {
        return shmget(key, 0, 0) != -1;
    }

     void destroy() const {
        if (shmctl(shmId_, IPC_RMID, nullptr) == -1) {
            throw ipc_exception("failed to destroy shared memory");
        }
        Logger::debug(tag_, "destroyed");
    }

private:
    static constexpr auto tag_ = "SharedMemory";
    key_t key_;
    int shmId_;
    T* data_ = nullptr;
    bool isOwner_;
    static constexpr int kPermissions = 0600;

    SharedMemory(const key_t key, const int id, const bool owner)
        : key_{key}, shmId_{id}, isOwner_{owner} {

        void* ptr = shmat(shmId_, nullptr, 0);
        if (ptr == reinterpret_cast<void*>(-1)) {
            throw ipc_exception("shmat failed");
        }
        data_ = static_cast<T*>(ptr);

        if (isOwner_) {
            new (data_) T();
        }
    }


};
