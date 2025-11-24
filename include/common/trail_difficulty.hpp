#pragma once

/**
 * Cyclist trail difficulty levels
 * T1 < T2 < T3 (ascending time)
 */
enum class TrailDifficulty {
    EASY,    // T1 - fastest descent
    MEDIUM,  // T2 - medium descent time
    HARD     // T3 - slowest descent (most difficult)
};
