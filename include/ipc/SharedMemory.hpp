#pragma once

#include <sys/ipc.h>
#include <sys/shm.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

/**
 * RAII wrapper for System V shared memory
 * Template class to support different structure types
 */
template<typename T>
class SharedMemory {
public:
    /**
     * Create or attach to shared memory segment
     * @param key IPC key for shared memory
     * @param create If true, creates new segment; if false, attaches to existing
     */
    explicit SharedMemory(key_t key, bool create = true)
        : key_{key}, shmId_{-1}, data_{nullptr}, isOwner_{create} {

        if (create) {
            shmId_ = shmget(key_, sizeof(T), IPC_CREAT | IPC_EXCL | 0600);
            if (shmId_ == -1) {
                perror("shmget (create)");
                throw std::runtime_error("Failed to create shared memory segment: " +
                    std::string(strerror(errno)));
            }
        } else {
            shmId_ = shmget(key_, sizeof(T), 0600);
            if (shmId_ == -1) {
                perror("shmget (attach)");
                throw std::runtime_error("Failed to get shared memory segment: " +
                    std::string(strerror(errno)));
            }
        }

        void* ptr = shmat(shmId_, nullptr, 0);
        if (ptr == reinterpret_cast<void*>(-1)) {
            perror("shmat");
            if (isOwner_) {
                shmctl(shmId_, IPC_RMID, nullptr);
            }
            throw std::runtime_error("Failed to attach shared memory: " +
                std::string(strerror(errno)));
        }
        data_ = static_cast<T*>(ptr);

        if (create) {
            new (data_) T();
        }
    }

    ~SharedMemory() {
        if (data_ != nullptr) {
            if (shmdt(data_) == -1) {
                perror("shmdt");
            }
        }
        if (isOwner_ && shmId_ != -1) {
            if (shmctl(shmId_, IPC_RMID, nullptr) == -1) {
                perror("shmctl IPC_RMID");
            }
        }
    }

    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    SharedMemory(SharedMemory&& other) noexcept
        : key_{other.key_}, shmId_{other.shmId_},
          data_{other.data_}, isOwner_{other.isOwner_} {
        other.shmId_ = -1;
        other.data_ = nullptr;
        other.isOwner_ = false;
    }

    SharedMemory& operator=(SharedMemory&& other) noexcept {
        if (this != &other) {
            if (data_ != nullptr) {
                shmdt(data_);
            }
            if (isOwner_ && shmId_ != -1) {
                shmctl(shmId_, IPC_RMID, nullptr);
            }

            key_ = other.key_;
            shmId_ = other.shmId_;
            data_ = other.data_;
            isOwner_ = other.isOwner_;

            other.shmId_ = -1;
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

    /**
     * Detach without removing the shared memory segment
     * Useful when transferring ownership to child processes
     */
    void detachOnly() {
        if (data_ != nullptr) {
            if (shmdt(data_) == -1) {
                perror("shmdt (detachOnly)");
            }
            data_ = nullptr;
        }
        isOwner_ = false;
    }

    /**
     * Remove the shared memory segment
     * Should only be called by the owner process
     */
    bool remove() {
        if (shmId_ != -1) {
            if (shmctl(shmId_, IPC_RMID, nullptr) == -1) {
                perror("shmctl IPC_RMID (remove)");
                return false;
            }
            isOwner_ = false;
            return true;
        }
        return false;
    }

    /**
     * Check if segment exists for given key
     */
    static bool exists(key_t key) {
        int id = shmget(key, 0, 0);
        return id != -1;
    }

    /**
     * Remove existing segment by key (cleanup utility)
     */
    static bool removeByKey(key_t key) {
        int id = shmget(key, 0, 0);
        if (id == -1) {
            return false;
        }
        if (shmctl(id, IPC_RMID, nullptr) == -1) {
            perror("shmctl IPC_RMID (removeByKey)");
            return false;
        }
        return true;
    }

private:
    key_t key_;
    int shmId_;
    T* data_;
    bool isOwner_;
};
