#pragma once

#include <csignal>
#include <unistd.h>
#include "logging/Logger.h"

namespace SignalHelper {
    inline constexpr auto tag = "SignalHelper";

    // Alias for backward compatibility
    struct Flags {
        volatile sig_atomic_t emergency{0};
        volatile sig_atomic_t resume{0};
        volatile sig_atomic_t exit{0};
    };

    using SignalFlags = Flags;

    // Mode enum for different process types
    enum class Mode {
        BASIC, // Only SIGTERM/SIGINT
        WORKER, // All signals including SIGUSR1/SIGUSR2
        TOURIST // All signals including SIGUSR1/SIGUSR2
    };

    namespace detail {
        inline Flags *g_flags = nullptr;

        // Pause tracking (main process only, points into shared memory)
        inline volatile time_t g_lastPauseStart = 0;
        inline time_t *g_totalPausedSeconds = nullptr;

        inline void handler(const int32_t sig) {
            // IMPORTANT: This handler must be async-signal-safe.
            // Only use signal-safe functions: write(), _exit(), kill(), signal flag assignment.
            // Do NOT use: std::exit(), printf(), malloc(), C++ iterators, etc.
            if (!g_flags) return;
            switch (sig) {
                case SIGUSR1:
                    g_flags->emergency = 1;
                    break;
                case SIGUSR2:
                    g_flags->resume = 1;
                    break;
                case SIGTERM:
                case SIGINT:
                    g_flags->exit = 1;
                    // Main loop will detect exit flag and call shutdown() for proper cleanup.
                    // Do NOT call std::exit() here - it's not signal-safe.
                    break;
                default:
                    break;
            }
        }

        /**
         * SIGTSTP handler (Ctrl+Z). Installed only in the main process.
         *
         * Flow:
         * 1. Record wall-clock time as pause start
         * 2. Reset SIGTSTP to SIG_DFL, raise(SIGTSTP) -> kernel stops the process
         * 3. On SIGCONT (fg), raise() returns here
         * 4. Compute paused duration and write to shared memory
         * 5. Re-install this handler for the next Ctrl+Z
         *
         * All functions used are async-signal-safe (POSIX):
         * time(), sigaction(), raise(), sigemptyset(), sigaddset(), sigprocmask()
         */
        inline void sigtstpHandler(int) {
            g_lastPauseStart = time(nullptr);

            // Reset to default so raise() actually stops the process
            struct sigaction sa{};
            sa.sa_handler = SIG_DFL;
            sigemptyset(&sa.sa_mask);
            sigaction(SIGTSTP, &sa, nullptr);

            // SIGTSTP is blocked during its own handler — unblock it
            // so that raise() actually stops the process immediately
            sigset_t tstp_mask;
            sigemptyset(&tstp_mask);
            sigaddset(&tstp_mask, SIGTSTP);
            sigprocmask(SIG_UNBLOCK, &tstp_mask, nullptr);

            raise(SIGTSTP);
            // === Process is stopped here by the kernel ===
            // === SIGCONT resumes execution here ===

            if (g_lastPauseStart > 0 && g_totalPausedSeconds != nullptr) {
                time_t pauseDuration = time(nullptr) - g_lastPauseStart;
                if (pauseDuration > 0) {
                    *g_totalPausedSeconds += pauseDuration;
                }
            }
            g_lastPauseStart = 0;

            // Re-install this handler for next Ctrl+Z
            sa.sa_handler = sigtstpHandler;
            sigaction(SIGTSTP, &sa, nullptr);
        }
    }

    // Note: Child process cleanup is handled by the main loop's shutdown() function,
    // not by the signal handler. This ensures proper cleanup order and avoids
    // async-signal-safety issues.

    // Original setup function (bool version)
    inline void setup(Flags &flags, const bool handleUserSignals) {
        detail::g_flags = &flags;

        struct sigaction sa{};
        sa.sa_handler = detail::handler;
        sigemptyset(&sa.sa_mask);

        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);

        if (handleUserSignals) {
            sigaction(SIGUSR1, &sa, nullptr);
            sigaction(SIGUSR2, &sa, nullptr);
        }

        Logger::debug(Logger::Source::Other, tag, "setup done, userSignals=%d", handleUserSignals);
    }

    // Mode-based setup function
    inline void setup(Flags &flags, Mode mode) {
        bool handleUserSignals = (mode == Mode::WORKER || mode == Mode::TOURIST);
        setup(flags, handleUserSignals);
    }

    /**
     * Install SIGTSTP handler for pause tracking (main process only).
     * @param totalPausedSeconds Pointer to totalPausedSeconds field in shared memory.
     */
    inline void setupPauseHandler(time_t *totalPausedSeconds) {
        detail::g_totalPausedSeconds = totalPausedSeconds;

        struct sigaction sa{};
        sa.sa_handler = detail::sigtstpHandler;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGTSTP, &sa, nullptr);
    }

    inline void ignoreChildren() {
        signal(SIGCHLD, SIG_IGN);
    }

    // Helper functions for checking signal states
    inline bool shouldExit(const Flags &flags) {
        return flags.exit != 0;
    }

    inline bool isEmergency(const Flags &flags) {
        return flags.emergency != 0;
    }

    inline bool isResumeRequested(const Flags &flags) {
        return flags.resume != 0;
    }

    inline void clearFlag(volatile sig_atomic_t &flag) {
        flag = 0;
    }
}
