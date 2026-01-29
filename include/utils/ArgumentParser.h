#pragma once

#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <unistd.h>
#include <sys/types.h>

/**
 * @brief Command-line argument parsing utilities.
 *
 * Provides type-safe parsing for process arguments and validation
 * for worker, cashier, and tourist processes.
 */
namespace ArgumentParser {
    namespace detail {
        /**
         * @brief Write error message to stderr.
         * @param msg Error message to display
         */
        inline void err(const char *msg) {
            char buf[256];
            int n = snprintf(buf, sizeof(buf), "Error: %s\n", msg);
            write(STDERR_FILENO, buf, n);
        }

        /**
         * @brief Write usage message to stderr.
         * @param program Program name (argv[0])
         * @param args Expected arguments description
         */
        inline void usage(const char *program, const char *args) {
            char buf[256];
            int n = snprintf(buf, sizeof(buf), "Usage: %s %s\n", program, args);
            write(STDERR_FILENO, buf, n);
        }
    }

    /**
     * @brief Parse string to uint32_t.
     * @param str Input string
     * @param out Output value
     * @return true if parsing succeeded, false otherwise
     */
    inline bool parseUint32(const char *str, uint32_t &out) {
        char *end;
        out = static_cast<uint32_t>(strtoul(str, &end, 10));
        return *end == '\0';
    }

    /**
     * @brief Parse string to int32_t.
     * @param str Input string
     * @param out Output value
     * @return true if parsing succeeded, false otherwise
     */
    inline bool parseInt32(const char *str, int32_t &out) {
        char *end;
        out = static_cast<int32_t>(strtol(str, &end, 10));
        return *end == '\0';
    }

    /**
     * @brief Parse string to key_t (IPC key).
     * @param str Input string
     * @param out Output key value
     * @return true if parsing succeeded, false otherwise
     */
    inline bool parseKeyT(const char *str, key_t &out) {
        char *end;
        out = static_cast<key_t>(strtol(str, &end, 10));
        return *end == '\0';
    }

    /**
     * @brief Parse string to boolean (0 or 1).
     * @param str Input string ("0" or "1")
     * @param out Output boolean value
     * @return true if parsing succeeded (valid 0 or 1), false otherwise
     */
    inline bool parseBool(const char *str, bool &out) {
        char *end;
        long val = strtol(str, &end, 10);
        if (*end != '\0' || val < 0 || val > 1) return false;
        out = (val == 1);
        return true;
    }

    /**
     * @brief Parse string to enum value within range.
     * @param str Input string
     * @param min Minimum valid value (inclusive)
     * @param max Maximum valid value (inclusive)
     * @param out Output integer value
     * @return true if parsing succeeded and value is in range, false otherwise
     */
    inline bool parseEnum(const char *str, int min, int max, int &out) {
        char *end;
        long val = strtol(str, &end, 10);
        if (*end != '\0' || val < min || val > max) return false;
        out = static_cast<int>(val);
        return true;
    }

    // ==================== Argument Structures ====================

    /**
     * @brief Arguments for worker (station controller) processes.
     */
    struct WorkerArgs {
        key_t shmKey;         ///< Shared memory key
        key_t semKey;         ///< Semaphore set key
        key_t msgKey;         ///< Worker message queue key
        key_t entryGateMsgKey; ///< Entry gate message queue key
        key_t logMsgKey;      ///< Log message queue key
    };

    /**
     * @brief Arguments for the cashier process.
     */
    struct CashierArgs {
        key_t shmKey;         ///< Shared memory key
        key_t semKey;         ///< Semaphore set key
        key_t cashierMsgKey;  ///< Cashier message queue key
        key_t logMsgKey;      ///< Log message queue key
    };

    /**
     * @brief Arguments for tourist processes.
     *
     * Children and bikes are handled as threads within the tourist process,
     * determined randomly based on Constants::Group::* chances.
     */
    struct TouristArgs {
        uint32_t id;
        uint32_t age;
        int type; // 0 = pedestrian, 1 = cyclist
        bool isVip;
        bool wantsToRide;
        uint32_t numChildren; // 0 = random, 1-2 = forced count for tests
        int trail; // 0-2 (easy, medium, hard)
        key_t shmKey;
        key_t semKey;
        key_t msgKey;
        key_t cashierMsgKey;
        key_t entryGateMsgKey;
        key_t logMsgKey;
    };

    // ==================== Parsers ====================

    /**
     * @brief Parse command-line arguments for worker process.
     * @param argc Argument count
     * @param argv Argument values
     * @param args Output WorkerArgs structure
     * @return true if all arguments were parsed successfully, false otherwise
     *
     * Expected: <shmKey> <semKey> <msgKey> <entryGateMsgKey> <logMsgKey>
     */
    inline bool parseWorkerArgs(int argc, char *argv[], WorkerArgs &args) {
        if (argc != 6) {
            detail::usage(argv[0], "<shmKey> <semKey> <msgKey> <entryGateMsgKey> <logMsgKey>");
            return false;
        }
        if (!parseKeyT(argv[1], args.shmKey)) {
            detail::err("Invalid shmKey");
            return false;
        }
        if (!parseKeyT(argv[2], args.semKey)) {
            detail::err("Invalid semKey");
            return false;
        }
        if (!parseKeyT(argv[3], args.msgKey)) {
            detail::err("Invalid msgKey");
            return false;
        }
        if (!parseKeyT(argv[4], args.entryGateMsgKey)) {
            detail::err("Invalid entryGateMsgKey");
            return false;
        }
        if (!parseKeyT(argv[5], args.logMsgKey)) {
            detail::err("Invalid logMsgKey");
            return false;
        }
        return true;
    }

    /**
     * @brief Parse command-line arguments for cashier process.
     * @param argc Argument count
     * @param argv Argument values
     * @param args Output CashierArgs structure
     * @return true if all arguments were parsed successfully, false otherwise
     *
     * Expected: <shmKey> <semKey> <cashierMsgKey> <logMsgKey>
     */
    inline bool parseCashierArgs(int argc, char *argv[], CashierArgs &args) {
        if (argc != 5) {
            detail::usage(argv[0], "<shmKey> <semKey> <cashierMsgKey> <logMsgKey>");
            return false;
        }
        if (!parseKeyT(argv[1], args.shmKey)) {
            detail::err("Invalid shmKey");
            return false;
        }
        if (!parseKeyT(argv[2], args.semKey)) {
            detail::err("Invalid semKey");
            return false;
        }
        if (!parseKeyT(argv[3], args.cashierMsgKey)) {
            detail::err("Invalid cashierMsgKey");
            return false;
        }
        if (!parseKeyT(argv[4], args.logMsgKey)) {
            detail::err("Invalid logMsgKey");
            return false;
        }
        return true;
    }

    /**
     * @brief Parse command-line arguments for tourist process.
     * @param argc Argument count
     * @param argv Argument values
     * @param args Output TouristArgs structure
     * @return true if all arguments were parsed successfully, false otherwise
     *
     * Expected (13 args): <id> <age> <type> <isVip> <wantsToRide> <trail> <shmKey> <semKey> <msgKey> <cashierMsgKey> <entryGateMsgKey> <logMsgKey>
     * Expected (14 args): <id> <age> <type> <isVip> <wantsToRide> <numChildren> <trail> <shmKey> <semKey> <msgKey> <cashierMsgKey> <entryGateMsgKey> <logMsgKey>
     */
    inline bool parseTouristArgs(int argc, char *argv[], TouristArgs &args) {
        // Accept 13 args (no numChildren) or 14 args (with numChildren)
        if (argc != 13 && argc != 14) {
            detail::usage(
                argv[0],
                "<id> <age> <type> <isVip> <wantsToRide> [numChildren] <trail> <shmKey> <semKey> <msgKey> <cashierMsgKey> <entryGateMsgKey> <logMsgKey>");
            return false;
        }

        bool hasNumChildren = (argc == 14);
        int offset = hasNumChildren ? 0 : -1;  // Shift indices if numChildren missing

        if (!parseUint32(argv[1], args.id)) {
            detail::err("Invalid id");
            return false;
        }
        if (!parseUint32(argv[2], args.age)) {
            detail::err("Invalid age");
            return false;
        }
        if (!parseEnum(argv[3], 0, 1, args.type)) {
            detail::err("Invalid type (0=pedestrian, 1=cyclist)");
            return false;
        }
        if (!parseBool(argv[4], args.isVip)) {
            detail::err("Invalid isVip (0-1)");
            return false;
        }
        if (!parseBool(argv[5], args.wantsToRide)) {
            detail::err("Invalid wantsToRide (0-1)");
            return false;
        }

        // numChildren: parse if present, else default to 0 (random generation)
        if (hasNumChildren) {
            if (!parseUint32(argv[6], args.numChildren)) {
                detail::err("Invalid numChildren");
                return false;
            }
            if (args.numChildren > 2) {
                detail::err("numChildren must be 0-2");
                return false;
            }
        } else {
            args.numChildren = 0;  // Default: random generation in TouristProcess
        }

        if (!parseEnum(argv[7 + offset], 0, 2, args.trail)) {
            detail::err("Invalid trail (0-2)");
            return false;
        }
        if (!parseKeyT(argv[8 + offset], args.shmKey)) {
            detail::err("Invalid shmKey");
            return false;
        }
        if (!parseKeyT(argv[9 + offset], args.semKey)) {
            detail::err("Invalid semKey");
            return false;
        }
        if (!parseKeyT(argv[10 + offset], args.msgKey)) {
            detail::err("Invalid msgKey");
            return false;
        }
        if (!parseKeyT(argv[11 + offset], args.cashierMsgKey)) {
            detail::err("Invalid cashierMsgKey");
            return false;
        }
        if (!parseKeyT(argv[12 + offset], args.entryGateMsgKey)) {
            detail::err("Invalid entryGateMsgKey");
            return false;
        }
        if (!parseKeyT(argv[13 + offset], args.logMsgKey)) {
            detail::err("Invalid logMsgKey");
            return false;
        }
        return true;
    }
}