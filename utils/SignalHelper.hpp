#pragma once

#include <csignal>
#include "logging/Logger.hpp"
#include "utils/ProcessSpawner.hpp"

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

        inline void handler(const int32_t sig) {
            if (!g_flags) return;
            switch (sig) {
                case SIGUSR1: g_flags->emergency = 1;
                    break;
                case SIGUSR2: g_flags->resume = 1;
                    break;
                case SIGTERM:
                case SIGINT: g_flags->exit = 1;
                    break;
                default: break;
            }
        }
    }

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

        Logger::debug(tag, "setup done, userSignals=%d", handleUserSignals);
    }

    // Mode-based setup function
    inline void setup(Flags &flags, Mode mode) {
        bool handleUserSignals = (mode == Mode::WORKER || mode == Mode::TOURIST);
        setup(flags, handleUserSignals);
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
