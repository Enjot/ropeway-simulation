#pragma once

#include <csignal>
#include <unistd.h>
#include <iostream>
#include "../Config.hpp"

/**
 * Centralized signal handling utilities for multi-process simulation.
 * Provides RAII-style signal management and common signal flags.
 *
 * Usage:
 *   SignalFlags flags;
 *   SignalHelper::setup(flags, SignalHelper::Mode::WORKER);
 *
 *   // In main loop:
 *   if (SignalHelper::isEmergency(flags)) { handleEmergency(); }
 *   if (SignalHelper::shouldExit(flags)) { break; }
 */
namespace SignalHelper {

    /**
     * Signal flags structure - stores signal state for a process.
     * Uses volatile sig_atomic_t for signal-safe access.
     */
    struct SignalFlags {
        volatile sig_atomic_t emergency{0};   // SIGUSR1 - emergency stop triggered
        volatile sig_atomic_t resume{0};      // SIGUSR2 - resume requested
        volatile sig_atomic_t shouldExit{0};  // SIGTERM/SIGINT - graceful shutdown
    };

    /**
     * Signal handling mode - determines which signals to register.
     * Different process types need different signal handling:
     * - BASIC: Only termination signals (for cashier)
     * - TOURIST: Emergency notification (SIGUSR1)
     * - WORKER: Both emergency and resume signals (SIGUSR1, SIGUSR2)
     * - ORCHESTRATOR: Termination + ignore child signals to prevent zombies
     */
    enum class Mode {
        BASIC,          // SIGTERM, SIGINT only (cashier)
        TOURIST,        // SIGUSR1 (emergency), SIGTERM, SIGINT
        WORKER,         // SIGUSR1, SIGUSR2, SIGTERM, SIGINT
        ORCHESTRATOR    // SIGTERM, SIGINT + ignore SIGCHLD
    };

    namespace detail {
        // Global pointer to current flags (set during setup)
        inline SignalFlags* g_flags = nullptr;

        /**
         * Signal handler function - updates appropriate flag based on signal.
         * Must be async-signal-safe, only modifies sig_atomic_t variables.
         */
        inline void signalHandler(int signum) {
            if (!g_flags) return;

            switch (signum) {
                case SIGUSR1:
                    g_flags->emergency = 1;
                    break;
                case SIGUSR2:
                    g_flags->resume = 1;
                    break;
                case SIGTERM:
                case SIGINT:
                    g_flags->shouldExit = 1;
                    break;
                default:
                    break;
            }
        }

        /**
         * Register a signal handler using sigaction (preferred over signal()).
         * @param signum Signal number to register
         * @return true on success, false on failure
         */
        inline bool registerSignal(int signum) {
            struct sigaction sa{};
            sa.sa_handler = signalHandler;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;

            if (sigaction(signum, &sa, nullptr) == -1) {
                perror("sigaction");
                return false;
            }
            return true;
        }
    }

    /**
     * Setup signal handlers for the given mode.
     * Must be called before entering main loop.
     *
     * @param flags Reference to SignalFlags that will receive signal notifications
     * @param mode  Signal handling mode (determines which signals to register)
     * @return true if all handlers registered successfully
     */
    inline bool setup(SignalFlags& flags, Mode mode) {
        detail::g_flags = &flags;
        bool success = true;

        // All modes handle SIGTERM and SIGINT for graceful shutdown
        success &= detail::registerSignal(SIGTERM);
        success &= detail::registerSignal(SIGINT);

        switch (mode) {
            case Mode::BASIC:
                // Only SIGTERM/SIGINT (already registered)
                break;

            case Mode::TOURIST:
                // Add SIGUSR1 for emergency stop notification from workers
                success &= detail::registerSignal(SIGUSR1);
                break;

            case Mode::WORKER:
                // Add both SIGUSR1 (emergency) and SIGUSR2 (resume)
                // Workers coordinate emergency stops between stations
                success &= detail::registerSignal(SIGUSR1);
                success &= detail::registerSignal(SIGUSR2);
                break;

            case Mode::ORCHESTRATOR:
                // Ignore SIGCHLD to prevent zombie processes
                // Main process spawns many children and doesn't wait for each
                signal(SIGCHLD, SIG_IGN);
                break;
        }

        return success;
    }

    /**
     * Wait while a flag is set (e.g., during emergency stop).
     * Uses pause() to efficiently wait for signals instead of busy-waiting.
     *
     * @param flag     Reference to the flag to wait on
     * @param exitFlag Reference to exit flag - returns early if set
     * @param logName  Process name for logging (optional)
     */
    inline void waitWhileSet(const volatile sig_atomic_t& flag,
                             const volatile sig_atomic_t& exitFlag,
                             const char* logName = nullptr) {
        if (logName) {
            std::cout << "[" << logName << "] Waiting for condition to clear..." << std::endl;
        }

        while (flag && !exitFlag) {
            // pause() blocks until a signal is delivered
            // It will return when any signal (including the one that clears the flag) arrives
            pause();
        }

        if (logName && !exitFlag) {
            std::cout << "[" << logName << "] Condition cleared, resuming" << std::endl;
        }
    }

    /**
     * Clear a signal flag after handling it.
     * Should be called after processing the signal to allow future signals.
     */
    inline void clearFlag(volatile sig_atomic_t& flag) {
        flag = 0;
    }

    /**
     * Check if graceful shutdown was requested (SIGTERM/SIGINT received).
     */
    inline bool shouldExit(const SignalFlags& flags) {
        return flags.shouldExit != 0;
    }

    /**
     * Check if emergency stop is active (SIGUSR1 received).
     */
    inline bool isEmergency(const SignalFlags& flags) {
        return flags.emergency != 0;
    }

    /**
     * Check if resume was requested (SIGUSR2 received).
     */
    inline bool isResumeRequested(const SignalFlags& flags) {
        return flags.resume != 0;
    }

} // namespace SignalHelper
