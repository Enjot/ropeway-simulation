#pragma once

/**
 * Ropeway operational state
 */
enum class RopewayState {
    STOPPED,        // Not operational
    RUNNING,        // Normal operation
    EMERGENCY_STOP, // Emergency stop (signal 1)
    CLOSING         // Closing sequence after Tk
};
