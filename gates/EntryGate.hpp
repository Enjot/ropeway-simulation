#pragma once

#include <cstdint>
#include <ctime>
#include <iostream>
#include "../ipc/core/Semaphore.hpp"
#include "ipc/RopewaySystemState.hpp"
#include "structures/Ticket.hpp"
#include "../Config.hpp"

/**
 * Entry gate validation result
 */
struct EntryValidationResult {
    bool allowed;
    uint32_t gateNumber;
    const char* reason;
};

/**
 * Entry gate logic - handles tourist entry to station area
 * - 4 entry gates
 * - VIP priority (bypass regular queue)
 * - Ticket validation
 * - Station capacity control via semaphore
 */
class EntryGate {
public:
    EntryGate(Semaphore& sem, RopewaySystemState* state)
        : sem_{sem}, state_{state} {}

    /**
     * Validate ticket for entry
     * @param ticketId Ticket ID
     * @param ticketType Type of ticket
     * @param validUntil Ticket expiration time
     * @param isVip VIP status
     * @return Validation result
     */
    EntryValidationResult validateTicket(uint32_t ticketId, TicketType ticketType,
                                          time_t validUntil, bool isVip) {
        EntryValidationResult result{false, 0, ""};

        // Check if ropeway is accepting tourists
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
            if (!state_->core.acceptingNewTourists) {
                result.reason = "Ropeway not accepting new tourists";
                return result;
            }
        }

        // Check ticket validity
        time_t now = time(nullptr);
        if (now > validUntil) {
            result.reason = "Ticket expired";
            return result;
        }

        // Check single-use ticket hasn't been used
        // (In a full implementation, would check against usage records)

        result.allowed = true;
        result.reason = "Valid ticket";
        return result;
    }

    /**
     * Attempt to enter through a gate
     * VIP tourists get priority (try non-blocking first)
     * Regular tourists wait in queue
     * @param touristId Tourist ID for logging
     * @param isVip VIP status
     * @param gateNumber Output: which gate was used
     * @return true if entry allowed, false otherwise
     */
    bool tryEnter(uint32_t touristId, bool isVip, uint32_t& gateNumber) {
        // Assign a gate (round-robin based on tourist ID)
        gateNumber = touristId % Config::Gate::NUM_ENTRY_GATES;

        if (isVip) {
            // VIP: try non-blocking first (priority)
            if (sem_.tryWait(Semaphore::Index::STATION_CAPACITY)) {
                std::cout << "[EntryGate " << gateNumber << "] VIP Tourist " << touristId
                          << " entered (priority)" << std::endl;
                return true;
            }
            // If can't enter immediately, still wait but with priority logging
            std::cout << "[EntryGate " << gateNumber << "] VIP Tourist " << touristId
                      << " waiting (station at capacity)" << std::endl;
        }

        // Regular tourists (or VIP if non-blocking failed): wait for capacity
        if (!sem_.wait(Semaphore::Index::STATION_CAPACITY)) {
            std::cerr << "[EntryGate " << gateNumber << "] Tourist " << touristId
                      << " failed to acquire station capacity" << std::endl;
            return false;
        }

        std::cout << "[EntryGate " << gateNumber << "] Tourist " << touristId
                  << " entered" << (isVip ? " [VIP]" : "") << std::endl;
        return true;
    }

    /**
     * Release station capacity (when tourist leaves station area)
     */
    void exitStation() {
        sem_.post(Semaphore::Index::STATION_CAPACITY, false);
    }

    /**
     * Get current station capacity
     */
    int getCurrentCapacity() {
        return sem_.getAvailableSpace(Semaphore::Index::STATION_CAPACITY);
    }

    /**
     * Get number of tourists currently in station
     */
    uint32_t getTouristsInStation() {
        return Config::Gate::MAX_TOURISTS_ON_STATION - getCurrentCapacity();
    }

private:
    Semaphore& sem_;
    RopewaySystemState* state_;
};
