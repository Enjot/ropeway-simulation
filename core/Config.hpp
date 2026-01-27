#pragma once

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "core/Constants.hpp"

/**
 * @brief Runtime configuration from environment variables.
 *
 * Edit ropeway.env, then: source ropeway.env && ./main
 *
 * For fixed requirements, see Constants.hpp.
 */
namespace Config {

    namespace Runtime {
        inline uint32_t getEnv(const char* envName) {
            const char* env = std::getenv(envName);
            if (!env) {
                throw std::runtime_error(std::string("Missing env: ") + envName);
            }
            return static_cast<uint32_t>(std::stoul(env));
        }

        inline float getEnvFloat(const char* envName) {
            const char* env = std::getenv(envName);
            if (!env) {
                throw std::runtime_error(std::string("Missing env: ") + envName);
            }
            return std::stof(env);
        }

        inline bool getEnvBool(const char* envName) {
            return getEnv(envName) != 0;
        }
    }

    namespace Time {
        inline uint32_t ONE_SECOND_US() { return 1'000'000; }
        inline uint32_t ONE_MINUTE_US() { return 60 * ONE_SECOND_US(); }

        inline uint32_t MAIN_LOOP_POLL_US() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_MAIN_LOOP_POLL_US");
            return v;
        }
        inline uint32_t ARRIVAL_DELAY_BASE_US() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_ARRIVAL_DELAY_BASE_US");
            return v;
        }
        inline uint32_t ARRIVAL_DELAY_RANDOM_US() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_ARRIVAL_DELAY_RANDOM_US");
            return v;
        }
    }

    namespace Simulation {
        constexpr uint32_t MAX_TOURIST_RECORDS{1500}; // Shared memory array size, must be constexpr

        inline uint32_t NUM_TOURISTS() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_NUM_TOURISTS");
            return v;
        }
        inline uint32_t STATION_CAPACITY() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_STATION_CAPACITY");
            return v;
        }
        inline uint32_t DURATION_US() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_DURATION_US");
            return v;
        }
        inline uint32_t OPENING_HOUR() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_OPENING_HOUR");
            return v;
        }
        inline uint32_t CLOSING_HOUR() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_CLOSING_HOUR");
            return v;
        }
        inline uint32_t TIME_SCALE() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_TIME_SCALE");
            return v;
        }
    }

    namespace Gate {
        inline uint32_t MAX_TOURISTS_ON_STATION() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_MAX_TOURISTS_ON_STATION");
            return v;
        }
    }

    namespace Chair {
        inline uint32_t RIDE_DURATION_US() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_RIDE_DURATION_US");
            return v;
        }
    }

    namespace Trail {
        inline uint32_t DURATION_EASY_US() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_TRAIL_EASY_US");
            return v;
        }
        inline uint32_t DURATION_MEDIUM_US() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_TRAIL_MEDIUM_US");
            return v;
        }
        inline uint32_t DURATION_HARD_US() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_TRAIL_HARD_US");
            return v;
        }
    }

    namespace Ticket {
        inline float SINGLE_USE_CHANCE() {
            static const float v = Runtime::getEnvFloat("ROPEWAY_TICKET_SINGLE_USE_PCT") / 100.0f;
            return v;
        }
        inline float TK1_CHANCE() {
            static const float v = Runtime::getEnvFloat("ROPEWAY_TICKET_TK1_PCT") / 100.0f;
            return v;
        }
        inline float TK2_CHANCE() {
            static const float v = Runtime::getEnvFloat("ROPEWAY_TICKET_TK2_PCT") / 100.0f;
            return v;
        }
        inline float TK3_CHANCE() {
            static const float v = Runtime::getEnvFloat("ROPEWAY_TICKET_TK3_PCT") / 100.0f;
            return v;
        }

        inline uint32_t TK1_DURATION_SEC() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_TK1_DURATION_SEC");
            return v;
        }
        inline uint32_t TK2_DURATION_SEC() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_TK2_DURATION_SEC");
            return v;
        }
        inline uint32_t TK3_DURATION_SEC() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_TK3_DURATION_SEC");
            return v;
        }
        inline uint32_t DAILY_DURATION_SEC() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_DAILY_DURATION_SEC");
            return v;
        }
    }

    namespace Logging {
        // Must be constexpr for use with if constexpr
        constexpr bool IS_DEBUG_ENABLED{false};
        constexpr bool IS_INFO_ENABLED{true};
        constexpr bool IS_WARN_ENABLED{true};
        constexpr bool IS_ERROR_ENABLED{true};
    }

    inline void validate() {
        Time::MAIN_LOOP_POLL_US();
        Time::ARRIVAL_DELAY_BASE_US();
        Time::ARRIVAL_DELAY_RANDOM_US();
        Simulation::NUM_TOURISTS();
        Simulation::STATION_CAPACITY();
        Simulation::DURATION_US();
        Simulation::OPENING_HOUR();
        Simulation::CLOSING_HOUR();
        Simulation::TIME_SCALE();
        Gate::MAX_TOURISTS_ON_STATION();
        Chair::RIDE_DURATION_US();
        Trail::DURATION_EASY_US();
        Trail::DURATION_MEDIUM_US();
        Trail::DURATION_HARD_US();
        Ticket::SINGLE_USE_CHANCE();
        Ticket::TK1_CHANCE();
        Ticket::TK2_CHANCE();
        Ticket::TK3_CHANCE();
        Ticket::TK1_DURATION_SEC();
        Ticket::TK2_DURATION_SEC();
        Ticket::TK3_DURATION_SEC();
        Ticket::DAILY_DURATION_SEC();
        // Logging flags are constexpr, no validation needed
    }
}
