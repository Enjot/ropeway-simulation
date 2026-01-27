#pragma once

#include <cstdint>

/**
 * @brief Compile-time flags.
 *
 * These values must be constexpr because they are used:
 * - For array sizes in shared memory (MAX_TOURIST_RECORDS)
 * - With if constexpr for conditional compilation (logging)
 */
namespace Flags {

    namespace Simulation {
        constexpr uint32_t MAX_TOURIST_RECORDS{1500}; // Shared memory array size
    }

    namespace Logging {
        constexpr bool IS_DEBUG_ENABLED{false};
        constexpr bool IS_INFO_ENABLED{true};
        constexpr bool IS_WARN_ENABLED{true};
        constexpr bool IS_ERROR_ENABLED{true};
    }

}
