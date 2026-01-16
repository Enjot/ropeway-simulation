#pragma once

#include <string>
#include <vector>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <csignal>
#include <sys/wait.h>
#include "../common/config.hpp"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

/**
 * Process spawning utilities for fork/exec operations.
 * Handles executable path resolution and child process management.
 */
namespace ProcessSpawner {

    /**
     * Get the path to a sibling executable.
     * Resolves the path relative to the current executable's directory.
     *
     * @param processName Name of the executable (e.g., "worker1_process")
     * @return Full path to the executable
     */
    inline std::string getExecutablePath(const char* processName) {
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
        char* lastSlash = strrchr(path, '/');
        if (lastSlash != nullptr) {
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
    inline pid_t spawn(const char* processName, const std::vector<std::string>& args) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            return -1;
        }

        if (pid == 0) {
            // Child process - execute target program
            std::string processPath = getExecutablePath(processName);

            // Build argv array (program name + args + nullptr)
            std::vector<char*> argv;
            argv.push_back(const_cast<char*>(processName));
            for (const auto& arg : args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);

            execv(processPath.c_str(), argv.data());
            perror("execv");
            _exit(1);  // Use _exit in child after fork
        }

        return pid;  // Parent returns child PID
    }

    /**
     * Spawn a process with three IPC keys (common pattern for workers/cashier).
     * Convenience wrapper around spawn().
     */
    inline pid_t spawnWithKeys(const char* processName, key_t key1, key_t key2, key_t key3) {
        return spawn(processName, {
            std::to_string(key1),
            std::to_string(key2),
            std::to_string(key3)
        });
    }

    /**
     * Terminate a process gracefully (SIGTERM) then forcefully (SIGKILL).
     *
     * @param pid Process ID to terminate
     * @param name Optional name for logging
     */
    inline void terminate(pid_t pid, const char* name = nullptr) {
        if (pid <= 0) return;

        if (name) {
            std::cout << "Terminating " << name << " (PID: " << pid << ")" << std::endl;
        }

        // Send SIGTERM for graceful termination
        if (kill(pid, SIGTERM) == -1 && errno == ESRCH) {
            // Process doesn't exist
            return;
        }

        // Poll with timeout for process to terminate
        // (SIGCHLD=SIG_IGN means blocking waitpid may hang on auto-reaped children)
        int status;
        for (int i = 0; i < 50; ++i) {  // 5 second timeout
            pid_t result = waitpid(pid, &status, WNOHANG);
            if (result == pid) {
                // Process exited
                return;
            }
            if (result == -1) {
                // ECHILD means already reaped or doesn't exist
                return;
            }
            usleep(100000);  // 100ms
        }

        // Process didn't terminate gracefully, force kill
        kill(pid, SIGKILL);
        waitpid(pid, &status, WNOHANG);
    }

    /**
     * Send SIGTERM to multiple processes.
     */
    inline void terminateAll(const std::vector<pid_t>& pids) {
        for (pid_t pid : pids) {
            if (pid > 0) {
                kill(pid, SIGTERM);
            }
        }
    }

    /**
     * Reap any zombie child processes (non-blocking).
     */
    inline void waitForAll() {
        while (waitpid(-1, nullptr, WNOHANG) > 0) {}
    }

} // namespace ProcessSpawner
