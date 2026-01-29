#pragma once

#include <cstdint>
#include <sys/types.h>

/**
 * Message types for entry gate priority
 * Lower value = higher priority (VIP first)
 */
namespace EntryGateMsgType {
    constexpr long VIP_REQUEST = 1; // VIPs get priority
    constexpr long REGULAR_REQUEST = 2; // Regular tourists
    constexpr long PRIORITY_RECEIVE = -2; // Receive with priority (gets lowest mtype first)
    constexpr long RESPONSE_BASE = 1000; // Response to specific tourist (+ touristId)
}

/**
 * Entry gate request from Tourist
 */
struct EntryGateRequest {
    uint32_t touristId;
    pid_t touristPid;
    bool isVip;

    EntryGateRequest() : touristId{0}, touristPid{0}, isVip{false} {
    }
};

/**
 * Entry gate response to Tourist
 */
struct EntryGateResponse {
    uint32_t touristId;
    bool allowed;

    EntryGateResponse() : touristId{0}, allowed{false} {
    }
};