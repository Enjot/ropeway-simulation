#pragma once

#include <string>
#include <vector>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <csignal>
#include <sys/wait.h>
#include "core/Config.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

/**
 * @brief Process lifecycle management utilities.
 *
 * Provides functions for spawning, terminating, and waiting for child processes
 * using fork/exec. All functions are signal-safe and handle EINTR.
 */
namespace ProcessSpawner {
    /**
     * @brief Get the full path to an executable in the same directory as current process.
     * @param processName Name of the target executable
     * @return Full path to the executable
     *
     * Uses platform-specific methods:
     * - macOS: _NSGetExecutablePath
     * - Linux: /proc/self/exe symlink
     */
    inline std::string getExecutablePath(const char *processName) {
        char path[1024];
        uint32_t size = sizeof(path);

#ifdef __APPLE__
        // macOS: use _NSGetExecutablePath
        if (_NSGetExecutablePath(path, &size) != 0) {
            return std::string("./") + processName;
        }
#else
        // Linux: read /proc/self/exe symlink
        ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (len == -1) {
            return std::string("./") + processName;
        }
        path[len] = '\0';
#endif

        // Replace executable name with target process name
        if (char *lastSlash = strrchr(path, '/'); lastSlash != nullptr) {
            strcpy(lastSlash + 1, processName);
            return path;
        }
        return std::string("./") + processName;
    }

    /**
     * Spawn a new process using fork/exec.
     * Parent process returns immediately with child PID.
     *
     * @param processName Name of the executable
     * @param args Vector of command-line arguments (excluding program name)
     * @return Child PID on success, -1 on failure
     */
    inline pid_t spawn(const char *processName, const std::vector<std::string> &args) {
        const pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            return -1;
        }

        if (pid == 0) {
            // Child process - execute target program
            const std::string processPath = getExecutablePath(processName);

            // Build argv array (program name + args + nullptr)
            std::vector<char *> argv;
            argv.push_back(const_cast<char *>(processName));
            for (const auto &arg: args) {
                argv.push_back(const_cast<char *>(arg.c_str()));
            }
            argv.push_back(nullptr);

            execv(processPath.c_str(), argv.data());
            perror("execv");
            _exit(1); // Use _exit in child after fork
        }

        return pid; // Parent returns child PID
    }

    /**
     * Spawn a process with three IPC keys (common pattern for cashier).
     * Convenience wrapper around spawn().
     */
    inline pid_t spawnWithKeys(const char *processName, const key_t key1, const key_t key2, const key_t key3) {
        return spawn(processName, {
                         std::to_string(key1),
                         std::to_string(key2),
                         std::to_string(key3)
                     });
    }

    /**
     * Spawn a process with four IPC keys (common pattern for cashier).
     * Convenience wrapper around spawn().
     */
    inline pid_t spawnWithKeys(const char *processName, const key_t key1, const key_t key2, const key_t key3,
                               const key_t key4) {
        return spawn(processName, {
                         std::to_string(key1),
                         std::to_string(key2),
                         std::to_string(key3),
                         std::to_string(key4)
                     });
    }

    /**
     * Spawn a process with five IPC keys (common pattern for workers).
     * Convenience wrapper around spawn().
     */
    inline pid_t spawnWithKeys(const char *processName, const key_t key1, const key_t key2, const key_t key3,
                               const key_t key4, const key_t key5) {
        return spawn(processName, {
                         std::to_string(key1),
                         std::to_string(key2),
                         std::to_string(key3),
                         std::to_string(key4),
                         std::to_string(key5)
                     });
    }

    /**
     * Terminate a process gracefully (SIGTERM) then forcefully (SIGKILL).
     *
     * @param pid Process ID to terminate
     * @param name Optional name for logging
     */
    inline void terminate(const pid_t pid, const char *name = nullptr) {
        if (pid <= 0) return;

        if (name) {
            std::cout << "Terminating " << name << " (PID: " << pid << ")" << std::endl;
        }

        // Send SIGTERM - process signal handler will set exit flag
        // With SIGCHLD=SIG_IGN, children are auto-reaped
        if (kill(pid, SIGTERM) == -1 && errno == ESRCH) {
            return; // Process doesn't exist
        }

        // Try to reap (may already be auto-reaped)
        int status;
        waitpid(pid, &status, WNOHANG);
    }

    /**
     * Send SIGTERM to multiple processes.
     */
    inline void terminateAll(const std::vector<pid_t> &pids) {
        for (const pid_t pid: pids) {
            if (pid > 0) {
                kill(pid, SIGTERM);
            }
        }
    }

    /**
     * Wait for a specific process to exit (blocking).
     * Handles ECHILD (already reaped) and EINTR (interrupted by signal).
     */
    inline void waitFor(const pid_t pid) {
        if (pid <= 0) return;
        int status;
        while (waitpid(pid, &status, 0) == -1) {
            if (errno == ECHILD) return;
            if (errno == EINTR) continue;
            perror("waitpid");
            return;
        }
    }

    /**
     * Reap any zombie child processes (non-blocking).
     */
    inline void waitForAll() {
        while (waitpid(-1, nullptr, WNOHANG) > 0) {
        }
    }
} // namespace ProcessSpawner