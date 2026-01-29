#pragma once

#include <sys/ipc.h>
#include <sys/shm.h>
#include <cerrno>
#include <cstdio>

#include "IpcException.h"
#include "logging/Logger.h"

/**
 * @brief RAII wrapper for System V shared memory segments.
 * @tparam T Type of data stored in shared memory
 *
 * Provides safe creation, attachment, and cleanup of shared memory.
 * The creator process (owner) is responsible for destruction.
 */
template<typename T>
class SharedMemory {
public:
    /**
     * @brief Construct by attaching to existing shared memory.
     * @param key System V IPC key
     * @param unused Unused parameter (for overload disambiguation)
     *
     * Used by child processes to attach to memory created by parent.
     */
    SharedMemory(const key_t key, bool /*unused*/)
        : SharedMemory(attach(key)) {
    }

    /**
     * @brief Create a new shared memory segment.
     * @param key System V IPC key
     * @return SharedMemory wrapper with ownership
     * @throws ipc_exception If creation fails
     *
     * If segment already exists, it is removed and recreated.
     * The caller becomes the owner responsible for cleanup.
     */
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

    /**
     * @brief Attach to an existing shared memory segment.
     * @param key System V IPC key
     * @return SharedMemory wrapper without ownership
     * @throws ipc_exception If attachment fails
     *
     * The caller is not responsible for cleanup (non-owner).
     */
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

    SharedMemory(const SharedMemory &) = delete;

    SharedMemory &operator=(const SharedMemory &) = delete;

    SharedMemory(SharedMemory &&other) noexcept
        : key_{other.key_}, shmId_{other.shmId_},
          data_{other.data_}, isOwner_{other.isOwner_} {
        other.data_ = nullptr;
        other.isOwner_ = false;
    }

    SharedMemory &operator=(SharedMemory &&other) noexcept {
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

    /** @brief Get raw pointer to shared data. */
    T *get() noexcept { return data_; }
    /** @brief Get const raw pointer to shared data. */
    const T *get() const noexcept { return data_; }

    /** @brief Access shared data via pointer. */
    T *operator->() noexcept { return data_; }
    /** @brief Access const shared data via pointer. */
    const T *operator->() const noexcept { return data_; }

    /** @brief Dereference to shared data. */
    T &operator*() noexcept { return *data_; }
    /** @brief Dereference to const shared data. */
    const T &operator*() const noexcept { return *data_; }

    /** @brief Get the System V shared memory ID. */
    int getId() const noexcept { return shmId_; }
    /** @brief Get the IPC key used to create/attach. */
    key_t getKey() const noexcept { return key_; }
    /** @brief Check if this wrapper is the owner (responsible for cleanup). */
    bool isOwner() const noexcept { return isOwner_; }

    /**
     * @brief Check if a shared memory segment exists.
     * @param key System V IPC key to check
     * @return true if segment exists, false otherwise
     */
    static bool exists(const key_t key) {
        return shmget(key, 0, 0) != -1;
    }

    /**
     * @brief Destroy the shared memory segment.
     * @throws ipc_exception If destruction fails
     */
    void destroy() const {
        if (shmctl(shmId_, IPC_RMID, nullptr) == -1) {
            perror("shmctl IPC_RMID");
            throw ipc_exception("failed to destroy shared memory");
        }
        Logger::debug(Logger::Source::Other, tag_, "destroyed");
    }

private:
    static constexpr auto tag_ = "SharedMemory";
    key_t key_;
    int shmId_;
    T *data_ = nullptr;
    bool isOwner_;
    static constexpr int kPermissions = 0600;

    SharedMemory(const key_t key, const int id, const bool owner)
        : key_{key}, shmId_{id}, isOwner_{owner} {
        void *ptr = shmat(shmId_, nullptr, 0);
        if (ptr == reinterpret_cast<void *>(-1)) {
            perror("shmat");
            throw ipc_exception("shmat failed");
        }
        data_ = static_cast<T *>(ptr);

        if (isOwner_) {
            new(data_) T();
        }
    }
};