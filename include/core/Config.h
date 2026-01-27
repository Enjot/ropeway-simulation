#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

#include "core/Constants.h"

#ifndef ROPEWAY_PROJECT_DIR
#define ROPEWAY_PROJECT_DIR "."
#endif

/**
 * @brief Runtime configuration from environment variables.
 *
 * Call Config::loadEnvFile() before using config values.
 *
 * For fixed requirements, see Constants.hpp.
 * For compile-time flags, see Flags.hpp.
 */
namespace Config {

    inline void loadEnvFile() {
        std::string path = std::string(ROPEWAY_PROJECT_DIR) + "/ropeway.env";
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open: " + path);
        }

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }

            // Remove "export " prefix if present
            const std::string exportPrefix = "export ";
            if (line.compare(0, exportPrefix.size(), exportPrefix) == 0) {
                line = line.substr(exportPrefix.size());
            }

            auto eqPos = line.find('=');
            if (eqPos == std::string::npos) {
                continue;
            }

            std::string key = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);

            setenv(key.c_str(), value.c_str(), 0);  // 0 = don't overwrite existing
        }
    }

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
