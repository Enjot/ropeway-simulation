#pragma once

#include "../Config.hpp"

/**
 * Cyclist trail difficulty levels
 * T1 < T2 < T3 (ascending time)
 */
enum class TrailDifficulty {
    EASY,
    MEDIUM,
    HARD
};

/**
 * @brief Convert TrailDifficulty enum to string representation.
 * @param trail TrailDifficulty to convert
 */
constexpr const char *toString(const TrailDifficulty trail) {
    switch (trail) {
        case TrailDifficulty::EASY: return "EASY";
        case TrailDifficulty::MEDIUM: return "MEDIUM";
        case TrailDifficulty::HARD: return "HARD";
        default: throw std::invalid_argument("Invalid TrailDifficulty value");
    }
}

/**
 * @brief Trail code (T1, T2, T3) for compact output.
 * @param trail TrailDifficulty to convert
 */
constexpr const char *toTrailCode(const TrailDifficulty trail) {
    switch (trail) {
        case TrailDifficulty::EASY: return "T1";
        case TrailDifficulty::MEDIUM: return "T2";
        case TrailDifficulty::HARD: return "T3";
        default: throw std::invalid_argument("Invalid TrailDifficulty value");
    }
}

constexpr uint32_t getDurationUs(const TrailDifficulty difficulty) {
    switch (difficulty) {
        case TrailDifficulty::EASY: return Config::Trail::DURATION_EASY_US;
        case TrailDifficulty::MEDIUM: return Config::Trail::DURATION_MEDIUM_US;
        case TrailDifficulty::HARD: return Config::Trail::DURATION_HARD_US;
        default: throw std::invalid_argument("Invalid TrailDifficulty value");
    }
}
