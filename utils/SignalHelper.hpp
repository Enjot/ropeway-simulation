#pragma once

#include <csignal>
#include "Logger.hpp"
#include "ProcessSpawner.hpp"

namespace SignalHelper {
    inline constexpr auto tag = "IpcManager";

    struct Flags {
        volatile sig_atomic_t emergency{0};
        volatile sig_atomic_t resume{0};
        volatile sig_atomic_t exit{0};
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

    inline void setup(Flags &flags, const bool handleUserSignals = false) {
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

    inline void ignoreChildren() {
        signal(SIGCHLD, SIG_IGN);
    }
}
