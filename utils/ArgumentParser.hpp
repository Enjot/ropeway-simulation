#pragma once

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <sys/types.h>

/**
 * Command-line argument parsing utilities for process executables.
 * Each process type (worker, cashier, tourist) has its own argument structure
 * and parsing function with validation and error messages.
 */
namespace ArgumentParser {

    /**
     * Generic result structure for parsing operations.
     */
    struct ParseResult {
        bool success{false};
        const char* errorMessage{nullptr};
    };

    // ==================== Primitive Parsers ====================

    /**
     * Parse unsigned 32-bit integer from string.
     * @param str Input string
     * @param out Output value
     * @return true if parsing succeeded
     */
    inline bool parseUint32(const char* str, uint32_t& out) {
        char* endPtr = nullptr;
        unsigned long val = std::strtoul(str, &endPtr, 10);
        if (*endPtr != '\0') return false;
        out = static_cast<uint32_t>(val);
        return true;
    }

    /**
     * Parse signed 32-bit integer from string.
     */
    inline bool parseInt32(const char* str, int32_t& out) {
        char* endPtr = nullptr;
        long val = std::strtol(str, &endPtr, 10);
        if (*endPtr != '\0') return false;
        out = static_cast<int32_t>(val);
        return true;
    }

    /**
     * Parse IPC key from string.
     */
    inline bool parseKeyT(const char* str, key_t& out) {
        char* endPtr = nullptr;
        long val = std::strtol(str, &endPtr, 10);
        if (*endPtr != '\0') return false;
        out = static_cast<key_t>(val);
        return true;
    }

    /**
     * Parse boolean (0 or 1) from string.
     */
    inline bool parseBool(const char* str, bool& out) {
        char* endPtr = nullptr;
        long val = std::strtol(str, &endPtr, 10);
        if (*endPtr != '\0' || val < 0 || val > 1) return false;
        out = (val == 1);
        return true;
    }

    /**
     * Parse enum value from string with range validation.
     * @param str Input string
     * @param minVal Minimum allowed value (inclusive)
     * @param maxVal Maximum allowed value (inclusive)
     * @param out Output value
     */
    inline bool parseEnum(const char* str, int minVal, int maxVal, int& out) {
        char* endPtr = nullptr;
        long val = std::strtol(str, &endPtr, 10);
        if (*endPtr != '\0' || val < minVal || val > maxVal) return false;
        out = static_cast<int>(val);
        return true;
    }

    // ==================== Worker Process Arguments ====================

    /**
     * Arguments for worker processes (worker1, worker2).
     * Workers need access to shared memory, semaphores, and message queue.
     */
    struct WorkerArgs {
        key_t shmKey;   // Shared memory key for RopewaySystemState
        key_t semKey;   // Semaphore set key
        key_t msgKey;   // Message queue key (for worker-to-worker communication)
    };

    /**
     * Parse worker process arguments.
     * Usage: <program> <shmKey> <semKey> <msgKey>
     */
    inline bool parseWorkerArgs(int argc, char* argv[], WorkerArgs& args) {
        if (argc != 4) {
            std::cerr << "Usage: " << argv[0] << " <shmKey> <semKey> <msgKey>\n";
            return false;
        }

        if (!parseKeyT(argv[1], args.shmKey)) {
            std::cerr << "Error: Invalid shmKey\n";
            return false;
        }
        if (!parseKeyT(argv[2], args.semKey)) {
            std::cerr << "Error: Invalid semKey\n";
            return false;
        }
        if (!parseKeyT(argv[3], args.msgKey)) {
            std::cerr << "Error: Invalid msgKey\n";
            return false;
        }

        return true;
    }

    // ==================== Cashier Process Arguments ====================

    /**
     * Arguments for cashier process.
     * Cashier needs shared memory, semaphores, and its own message queue for tickets.
     */
    struct CashierArgs {
        key_t shmKey;         // Shared memory key for RopewaySystemState
        key_t semKey;         // Semaphore set key
        key_t cashierMsgKey;  // Message queue key (for ticket requests/responses)
    };

    /**
     * Parse cashier process arguments.
     * Usage: <program> <shmKey> <semKey> <cashierMsgKey>
     */
    inline bool parseCashierArgs(int argc, char* argv[], CashierArgs& args) {
        if (argc != 4) {
            std::cerr << "Usage: " << argv[0] << " <shmKey> <semKey> <cashierMsgKey>\n";
            return false;
        }

        if (!parseKeyT(argv[1], args.shmKey)) {
            std::cerr << "Error: Invalid shmKey\n";
            return false;
        }
        if (!parseKeyT(argv[2], args.semKey)) {
            std::cerr << "Error: Invalid semKey\n";
            return false;
        }
        if (!parseKeyT(argv[3], args.cashierMsgKey)) {
            std::cerr << "Error: Invalid cashierMsgKey\n";
            return false;
        }

        return true;
    }

    // ==================== Tourist Process Arguments ====================

    /**
     * Arguments for tourist process.
     * Contains all tourist-specific data plus IPC keys.
     */
    struct TouristArgs {
        uint32_t id;          // Unique tourist identifier
        uint32_t age;         // Tourist age (affects discounts and supervision)
        int type;             // TouristType: 0=PEDESTRIAN, 1=CYCLIST
        bool isVip;           // Request VIP status
        bool wantsToRide;     // Whether tourist wants to ride (some just walk around)
        int32_t guardianId;   // Guardian tourist ID for children (-1 if none)
        int trail;            // TrailDifficulty: 0=EASY, 1=MEDIUM, 2=HARD
        key_t shmKey;         // Shared memory key
        key_t semKey;         // Semaphore set key
        key_t msgKey;         // Worker message queue key
        key_t cashierMsgKey;  // Cashier message queue key
    };

    /**
     * Parse tourist process arguments.
     * Usage: <program> <id> <age> <type> <isVip> <wantsToRide> <guardianId> <trail>
     *                  <shmKey> <semKey> <msgKey> <cashierMsgKey>
     */
    inline bool parseTouristArgs(int argc, char* argv[], TouristArgs& args) {
        if (argc != 12) {
            std::cerr << "Usage: " << argv[0]
                      << " <id> <age> <type> <isVip> <wantsToRide> <guardianId> <trail> <shmKey> <semKey> <msgKey> <cashierMsgKey>\n";
            return false;
        }

        if (!parseUint32(argv[1], args.id)) {
            std::cerr << "Error: Invalid id\n";
            return false;
        }
        if (!parseUint32(argv[2], args.age)) {
            std::cerr << "Error: Invalid age\n";
            return false;
        }
        if (!parseEnum(argv[3], 0, 1, args.type)) {
            std::cerr << "Error: Invalid type (must be 0 or 1)\n";
            return false;
        }
        if (!parseBool(argv[4], args.isVip)) {
            std::cerr << "Error: Invalid isVip (must be 0 or 1)\n";
            return false;
        }
        if (!parseBool(argv[5], args.wantsToRide)) {
            std::cerr << "Error: Invalid wantsToRide (must be 0 or 1)\n";
            return false;
        }
        if (!parseInt32(argv[6], args.guardianId)) {
            std::cerr << "Error: Invalid guardianId\n";
            return false;
        }
        if (!parseEnum(argv[7], 0, 2, args.trail)) {
            std::cerr << "Error: Invalid trail (must be 0, 1, or 2)\n";
            return false;
        }
        if (!parseKeyT(argv[8], args.shmKey)) {
            std::cerr << "Error: Invalid shmKey\n";
            return false;
        }
        if (!parseKeyT(argv[9], args.semKey)) {
            std::cerr << "Error: Invalid semKey\n";
            return false;
        }
        if (!parseKeyT(argv[10], args.msgKey)) {
            std::cerr << "Error: Invalid msgKey\n";
            return false;
        }
        if (!parseKeyT(argv[11], args.cashierMsgKey)) {
            std::cerr << "Error: Invalid cashierMsgKey\n";
            return false;
        }

        return true;
    }

} // namespace ArgumentParser
