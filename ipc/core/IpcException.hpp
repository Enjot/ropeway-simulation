#pragma once

#include <stdexcept>
#include <string>

class ipc_exception : public std::runtime_error {
public:
    explicit ipc_exception(const char *message) : std::runtime_error(message) {
    }

    explicit ipc_exception(const std::string &message) : std::runtime_error(message) {
    }
};
