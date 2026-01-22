#pragma once

#include <sys/ipc.h>
#include <sys/shm.h>
#include <cerrno>
#include <cstdio>

#include "IpcException.hpp"

template<typename T>
class SharedMemory {
public:
    static SharedMemory create(const key_t key) {
        int id = shmget(key, sizeof(T), IPC_CREAT | IPC_EXCL | kPermissions);
        if (id == -1 && errno == EEXIST) {
            shmctl(shmget(key, 0, 0), IPC_RMID, nullptr);
            id = shmget(key, sizeof(T), IPC_CREAT | IPC_EXCL | kPermissions);
        }
        if (id == -1) {
            perror("shmget create");
            throw ipc_exception("shmget create failed");
        }
        return SharedMemory(key, id, true);
    }

    static SharedMemory attach(const key_t key) {
        const int id = shmget(key, 0, 0);
        if (id == -1) {
            perror("shmget attach");
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

    [[nodiscard]] int getId() const noexcept { return shmId_; }
    [[nodiscard]] key_t getKey() const noexcept { return key_; }
    [[nodiscard]] bool isOwner() const noexcept { return isOwner_; }

    static bool exists(const key_t key) {
        return shmget(key, 0, 0) != -1;
    }

    static bool removeByKey(const key_t key) {
        const int id = shmget(key, 0, 0);
        if (id == -1) return false;
        return shmctl(id, IPC_RMID, nullptr) != -1;
    }

private:
    static constexpr int kPermissions = 0600;

    SharedMemory(const key_t key, const int id, const bool owner)
        : key_{key}, shmId_{id}, isOwner_{owner} {

        void* ptr = shmat(shmId_, nullptr, 0);
        if (ptr == reinterpret_cast<void*>(-1)) {
            perror("shmat");
            throw ipc_exception("shmat failed");
        }
        data_ = static_cast<T*>(ptr);

        if (isOwner_) {
            new (data_) T();
        }
    }

    key_t key_;
    int shmId_;
    T* data_ = nullptr;
    bool isOwner_;
};
