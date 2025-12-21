#pragma once

/**
 * Type of pass/ticket
 */
enum class PassType {
    SINGLE_USE,  // Single ride
    TIME_TK1,    // Time-based pass type 1
    TIME_TK2,    // Time-based pass type 2
    TIME_TK3,    // Time-based pass type 3
    DAILY        // Valid for the entire day
};
