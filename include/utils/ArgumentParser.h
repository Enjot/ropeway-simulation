#pragma once

#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <unistd.h>
#include <sys/types.h>

namespace ArgumentParser {
    namespace detail {
        inline void err(const char *msg) {
            char buf[256];
            int n = snprintf(buf, sizeof(buf), "Error: %s\n", msg);
            write(STDERR_FILENO, buf, n);
        }

        inline void usage(const char *program, const char *args) {
            char buf[256];
            int n = snprintf(buf, sizeof(buf), "Usage: %s %s\n", program, args);
            write(STDERR_FILENO, buf, n);
        }
    }

    inline bool parseUint32(const char *str, uint32_t &out) {
        char *end;
        out = static_cast<uint32_t>(strtoul(str, &end, 10));
        return *end == '\0';
    }

    inline bool parseInt32(const char *str, int32_t &out) {
        char *end;
        out = static_cast<int32_t>(strtol(str, &end, 10));
        return *end == '\0';
    }

    inline bool parseKeyT(const char *str, key_t &out) {
        char *end;
        out = static_cast<key_t>(strtol(str, &end, 10));
        return *end == '\0';
    }

    inline bool parseBool(const char *str, bool &out) {
        char *end;
        long val = strtol(str, &end, 10);
        if (*end != '\0' || val < 0 || val > 1) return false;
        out = (val == 1);
        return true;
    }

    inline bool parseEnum(const char *str, int min, int max, int &out) {
        char *end;
        long val = strtol(str, &end, 10);
        if (*end != '\0' || val < min || val > max) return false;
        out = static_cast<int>(val);
        return true;
    }

    // ==================== Argument Structures ====================

    struct WorkerArgs {
        key_t shmKey;
        key_t semKey;
        key_t msgKey;
        key_t entryGateMsgKey;
    };

    struct CashierArgs {
        key_t shmKey;
        key_t semKey;
        key_t cashierMsgKey;
    };

    struct TouristArgs {
        uint32_t id;
        uint32_t age;
        int type;
        bool isVip;
        bool wantsToRide;
        int32_t guardianId;
        uint32_t numChildren; // Number of children this adult will spawn (0-2)
        int trail;
        key_t shmKey;
        key_t semKey;
        key_t msgKey;
        key_t cashierMsgKey;
        key_t entryGateMsgKey;
    };

    // ==================== Parsers ====================

    inline bool parseWorkerArgs(int argc, char *argv[], WorkerArgs &args) {
        if (argc != 5) {
            detail::usage(argv[0], "<shmKey> <semKey> <msgKey> <entryGateMsgKey>");
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
        return true;
    }

    inline bool parseCashierArgs(int argc, char *argv[], CashierArgs &args) {
        if (argc != 4) {
            detail::usage(argv[0], "<shmKey> <semKey> <cashierMsgKey>");
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
        return true;
    }

    inline bool parseTouristArgs(int argc, char *argv[], TouristArgs &args) {
        if (argc != 14) {
            detail::usage(
                argv[0],
                "<id> <age> <type> <isVip> <wantsToRide> <guardianId> <numChildren> <trail> <shmKey> <semKey> <msgKey> <cashierMsgKey> <entryGateMsgKey>");
            return false;
        }
        if (!parseUint32(argv[1], args.id)) {
            detail::err("Invalid id");
            return false;
        }
        if (!parseUint32(argv[2], args.age)) {
            detail::err("Invalid age");
            return false;
        }
        if (!parseEnum(argv[3], 0, 1, args.type)) {
            detail::err("Invalid type (0-1)");
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
        if (!parseInt32(argv[6], args.guardianId)) {
            detail::err("Invalid guardianId");
            return false;
        }
        if (!parseUint32(argv[7], args.numChildren)) {
            detail::err("Invalid numChildren");
            return false;
        }
        if (!parseEnum(argv[8], 0, 2, args.trail)) {
            detail::err("Invalid trail (0-2)");
            return false;
        }
        if (!parseKeyT(argv[9], args.shmKey)) {
            detail::err("Invalid shmKey");
            return false;
        }
        if (!parseKeyT(argv[10], args.semKey)) {
            detail::err("Invalid semKey");
            return false;
        }
        if (!parseKeyT(argv[11], args.msgKey)) {
            detail::err("Invalid msgKey");
            return false;
        }
        if (!parseKeyT(argv[12], args.cashierMsgKey)) {
            detail::err("Invalid cashierMsgKey");
            return false;
        }
        if (!parseKeyT(argv[13], args.entryGateMsgKey)) {
            detail::err("Invalid entryGateMsgKey");
            return false;
        }
        return true;
    }
}