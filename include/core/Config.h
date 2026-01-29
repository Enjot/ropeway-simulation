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
 * For fixed requirements, see Constants.h.
 * For compile-time flags, see Flags.h.
 */
namespace Config {
    namespace Runtime {
        // Forward declaration (defined below)
        inline uint32_t getEnv(const char *envName);
        inline float getEnvFloat(const char *envName);

        /**
         * @brief Get float environment variable with default fallback.
         * @param envName Name of the environment variable
         * @param defaultValue Value to return if variable is not set
         * @return Parsed float value or default
         */
        inline float getEnvFloatOr(const char *envName, float defaultValue) {
            const char *env = std::getenv(envName);
            if (!env) {
                return defaultValue;
            }
            return std::stof(env);
        }

        /**
         * @brief Get uint32 environment variable with default fallback.
         * @param envName Name of the environment variable
         * @param defaultValue Value to return if variable is not set
         * @return Parsed uint32 value or default
         */
        inline uint32_t getEnvOr(const char *envName, uint32_t defaultValue) {
            const char *env = std::getenv(envName);
            if (!env) {
                return defaultValue;
            }
            return static_cast<uint32_t>(std::stoul(env));
        }
    }

    /**
     * @brief Load configuration from ropeway.env file.
     *
     * Reads key=value pairs from the env file and sets them as environment
     * variables. Existing environment variables are not overwritten.
     *
     * @throws std::runtime_error If the env file cannot be opened
     */
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

            setenv(key.c_str(), value.c_str(), 0); // 0 = don't overwrite existing
        }
    }

    namespace Runtime {
        /**
         * @brief Get required uint32 environment variable.
         * @param envName Name of the environment variable
         * @return Parsed uint32 value
         * @throws std::runtime_error If variable is not set
         */
        inline uint32_t getEnv(const char *envName) {
            const char *env = std::getenv(envName);
            if (!env) {
                throw std::runtime_error(std::string("Missing env: ") + envName);
            }
            return static_cast<uint32_t>(std::stoul(env));
        }

        /**
         * @brief Get required float environment variable.
         * @param envName Name of the environment variable
         * @return Parsed float value
         * @throws std::runtime_error If variable is not set
         */
        inline float getEnvFloat(const char *envName) {
            const char *env = std::getenv(envName);
            if (!env) {
                throw std::runtime_error(std::string("Missing env: ") + envName);
            }
            return std::stof(env);
        }

        /**
         * @brief Get required boolean environment variable.
         * @param envName Name of the environment variable
         * @return true if value is non-zero, false otherwise
         * @throws std::runtime_error If variable is not set
         */
        inline bool getEnvBool(const char *envName) {
            return getEnv(envName) != 0;
        }
    }

    /**
     * @brief Time-related configuration values.
     */
    namespace Time {
        /** @brief One second in microseconds (1,000,000) */
        inline uint32_t ONE_SECOND_US() { return 1'000'000; }

        /** @brief One minute in microseconds (60,000,000) */
        inline uint32_t ONE_MINUTE_US() { return 60 * ONE_SECOND_US(); }

        /** @brief Main loop polling interval in microseconds */
        inline uint32_t MAIN_LOOP_POLL_US() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_MAIN_LOOP_POLL_US");
            return v;
        }

        /** @brief Base delay between tourist arrivals in microseconds */
        inline uint32_t ARRIVAL_DELAY_BASE_US() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_ARRIVAL_DELAY_BASE_US");
            return v;
        }

        /** @brief Random component added to arrival delay in microseconds */
        inline uint32_t ARRIVAL_DELAY_RANDOM_US() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_ARRIVAL_DELAY_RANDOM_US");
            return v;
        }
    }

    /**
     * @brief Core simulation parameters.
     */
    namespace Simulation {
        /** @brief Total number of tourists to spawn */
        inline uint32_t NUM_TOURISTS() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_NUM_TOURISTS");
            return v;
        }

        /** @brief Maximum tourists allowed in lower station */
        inline uint32_t STATION_CAPACITY() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_STATION_CAPACITY");
            return v;
        }

        /** @brief Total simulation duration in microseconds */
        inline uint32_t DURATION_US() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_DURATION_US");
            return v;
        }

        /** @brief Simulated opening hour (0-23) */
        inline uint32_t OPENING_HOUR() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_OPENING_HOUR");
            return v;
        }

        /** @brief Simulated closing hour (0-23) */
        inline uint32_t CLOSING_HOUR() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_CLOSING_HOUR");
            return v;
        }

        /** @brief Time scale factor (real seconds to simulated seconds) */
        inline uint32_t TIME_SCALE() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_TIME_SCALE");
            return v;
        }
    }

    /**
     * @brief Chair/lift configuration.
     */
    namespace Chair {
        /** @brief Duration of chair ride from lower to upper station in microseconds */
        inline uint32_t RIDE_DURATION_US() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_RIDE_DURATION_US");
            return v;
        }
    }

    /**
     * @brief Trail duration configuration for cyclists.
     */
    namespace Trail {
        /** @brief Duration of easy trail (T1) in microseconds */
        inline uint32_t DURATION_EASY_US() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_TRAIL_EASY_US");
            return v;
        }

        /** @brief Duration of medium trail (T2) in microseconds */
        inline uint32_t DURATION_MEDIUM_US() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_TRAIL_MEDIUM_US");
            return v;
        }

        /** @brief Duration of hard trail (T3) in microseconds */
        inline uint32_t DURATION_HARD_US() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_TRAIL_HARD_US");
            return v;
        }
    }

    /**
     * @brief Ticket type probabilities and durations.
     */
    namespace Ticket {
        /** @brief Probability of tourist buying single-use ticket (0.0-1.0) */
        inline float SINGLE_USE_CHANCE() {
            static const float v = Runtime::getEnvFloat("ROPEWAY_TICKET_SINGLE_USE_PCT") / 100.0f;
            return v;
        }

        /** @brief Probability of tourist buying TK1 (1-hour) ticket (0.0-1.0) */
        inline float TK1_CHANCE() {
            static const float v = Runtime::getEnvFloat("ROPEWAY_TICKET_TK1_PCT") / 100.0f;
            return v;
        }

        /** @brief Probability of tourist buying TK2 (2-hour) ticket (0.0-1.0) */
        inline float TK2_CHANCE() {
            static const float v = Runtime::getEnvFloat("ROPEWAY_TICKET_TK2_PCT") / 100.0f;
            return v;
        }

        /** @brief Probability of tourist buying TK3 (4-hour) ticket (0.0-1.0) */
        inline float TK3_CHANCE() {
            static const float v = Runtime::getEnvFloat("ROPEWAY_TICKET_TK3_PCT") / 100.0f;
            return v;
        }

        /** @brief Duration of TK1 ticket validity in seconds */
        inline uint32_t TK1_DURATION_SEC() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_TK1_DURATION_SEC");
            return v;
        }

        /** @brief Duration of TK2 ticket validity in seconds */
        inline uint32_t TK2_DURATION_SEC() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_TK2_DURATION_SEC");
            return v;
        }

        /** @brief Duration of TK3 ticket validity in seconds */
        inline uint32_t TK3_DURATION_SEC() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_TK3_DURATION_SEC");
            return v;
        }

        /** @brief Duration of daily ticket validity in seconds */
        inline uint32_t DAILY_DURATION_SEC() {
            static const uint32_t v = Runtime::getEnv("ROPEWAY_DAILY_DURATION_SEC");
            return v;
        }
    }

    /**
     * Test configuration namespace.
     * These optional environment variables allow tests to override defaults.
     * If not set, uses production defaults from Constants.
     */
    namespace Test {
        /**
         * VIP chance: 0-100 (percentage).
         * Default: Constants::Vip::VIP_CHANCE * 100.0f (1%)
         */
        inline float VIP_CHANCE_PCT() {
            static float value = Runtime::getEnvFloatOr("ROPEWAY_VIP_CHANCE_PCT",
                                                        Constants::Vip::VIP_CHANCE * 100.0f);
            return value;
        }

        /**
         * Child chance: 0-100 (percentage).
         * Default: Constants::Group::CHILD_CHANCE * 100.0f (20%)
         */
        inline float CHILD_CHANCE_PCT() {
            static float value = Runtime::getEnvFloatOr("ROPEWAY_CHILD_CHANCE_PCT",
                                                        Constants::Group::CHILD_CHANCE * 100.0f);
            return value;
        }

        /**
         * Force emergency at specific elapsed second (0 = disabled/random).
         * When > 0, worker triggers emergency at this exact second instead of random.
         */
        inline uint32_t FORCE_EMERGENCY_AT_SEC() {
            static uint32_t value = Runtime::getEnvOr("ROPEWAY_FORCE_EMERGENCY_AT_SEC", 0);
            return value;
        }

        /**
         * Percentage of tourists that want to ride (0-100).
         * Default: 90% (10% of tourists don't want to ride).
         * Set to 100 for stress testing to ensure all tourists complete full lifecycle.
         */
        inline float WANTS_TO_RIDE_PCT() {
            static float value = Runtime::getEnvFloatOr("ROPEWAY_WANTS_TO_RIDE_PCT");
            return value;
        }
    }

    /**
     * @brief Validate all required configuration values.
     *
     * Attempts to load all configuration values, which will throw an exception
     * if any required environment variable is missing.
     *
     * @throws std::runtime_error If any required configuration is missing
     */
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
        // Test config is optional, no validation needed
    }
}