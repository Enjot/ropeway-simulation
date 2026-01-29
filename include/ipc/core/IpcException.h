#pragma once

#include <stdexcept>
#include <string>

/**
 * @brief Exception type for IPC-related errors.
 *
 * Thrown when System V IPC operations fail (shmget, semget, msgget, etc.).
 */
class ipc_exception : public std::runtime_error {
public:
    /**
     * @brief Construct exception with C-string message.
     * @param message Error description
     */
    explicit ipc_exception(const char *message) : std::runtime_error(message) {
    }

    /**
     * @brief Construct exception with std::string message.
     * @param message Error description
     */
    explicit ipc_exception(const std::string &message) : std::runtime_error(message) {
    }
};