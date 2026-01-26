#pragma once

#include <cstdint>
#include <vector>
#include <iostream>
#include "ipc/core/Semaphore.hpp"
#include "ipc/core/SharedMemory.hpp"
#include "ipc/RopewaySystemState.hpp"
#include "structures/Tourist.hpp"
#include "structures/Chair.hpp"
#include "../Config.hpp"

/**
 * Ride gate validation result
 */
struct RideValidationResult {
    bool allowed;
    uint32_t gateNumber;
    uint32_t chairId;
    const char* reason;
};

/**
 * Passenger group for a chair
 */
struct PassengerGroup {
    std::vector<uint32_t> touristIds;
    uint32_t slotsUsed;
    uint32_t cyclistCount;
    uint32_t pedestrianCount;
    bool hasAdult;
    uint32_t childrenNeedingSupervision;

    PassengerGroup() : slotsUsed{0}, cyclistCount{0}, pedestrianCount{0},
                       hasAdult{false}, childrenNeedingSupervision{0} {}

    bool canAddTourist(const Tourist& tourist) const {
        uint32_t slotCost = tourist.getSlotCost();

        // Check slot capacity
        if (slotsUsed + slotCost > Config::Chair::SLOTS_PER_CHAIR) {
            return false;
        }

        // Check cyclist limit (max 2 per chair)
        if (tourist.type == TouristType::CYCLIST &&
            cyclistCount >= Config::Chair::MAX_CYCLISTS_PER_CHAIR) {
            return false;
        }

        // Check child supervision rules
        if (tourist.needsSupervision()) {
            // Child needs an adult in the group
            if (!hasAdult && tourist.guardianId == -1) {
                return false;
            }
            // Check if adult can take more children
            if (childrenNeedingSupervision >= Config::Gate::MAX_CHILDREN_PER_ADULT) {
                return false;
            }
        }

        return true;
    }

    void addTourist(const Tourist& tourist) {
        touristIds.push_back(tourist.id);
        slotsUsed += tourist.getSlotCost();

        if (tourist.type == TouristType::CYCLIST) {
            cyclistCount++;
        } else {
            pedestrianCount++;
        }

        if (tourist.isAdult()) {
            hasAdult = true;
        }

        if (tourist.needsSupervision()) {
            childrenNeedingSupervision++;
        }
    }

    bool isFull() const {
        return slotsUsed >= Config::Chair::SLOTS_PER_CHAIR;
    }

    bool isEmpty() const {
        return touristIds.empty();
    }
};

/**
 * Ride gate logic - handles boarding onto chairs
 * - 3 ride gates controlled by LowerWorker
 * - Child supervision enforcement
 * - Chair slot allocation
 */
class RideGate {
public:
    RideGate(Semaphore& sem, RopewaySystemState* state)
        : sem_{sem}, state_{state}, nextChairId_{0} {}

    /**
     * Validate if a tourist can board
     * @param tourist Tourist attempting to board
     * @param group Current passenger group for the chair
     * @return Validation result
     */
    RideValidationResult validateBoarding(const Tourist& tourist, const PassengerGroup& group) {
        RideValidationResult result{false, 0, 0, ""};

        // Check ropeway state
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
            if (state_->core.state == RopewayState::EMERGENCY_STOP) {
                result.reason = "Emergency stop active";
                return result;
            }
            if (state_->core.state == RopewayState::STOPPED) {
                result.reason = "Ropeway stopped";
                return result;
            }
        }

        // Check child supervision
        if (tourist.needsSupervision()) {
            if (tourist.guardianId == -1 && !group.hasAdult) {
                result.reason = "Child requires adult supervision";
                return result;
            }
        }

        // Check if can be added to group
        if (!group.canAddTourist(tourist)) {
            result.reason = "Cannot fit in current chair group";
            return result;
        }

        result.allowed = true;
        result.reason = "Boarding allowed";
        return result;
    }

    /**
     * Check if a child can board with a specific guardian
     */
    bool canBoardWithGuardian(const Tourist& child, const Tourist& guardian) {
        // Guardian must be adult
        if (!guardian.isAdult()) {
            return false;
        }

        // Guardian must not already have max children
        if (guardian.dependentCount >= Config::Gate::MAX_CHILDREN_PER_ADULT) {
            return false;
        }

        return true;
    }

    /**
     * Allocate a chair for boarding
     * @return Chair ID, or -1 if no chairs available
     */
    int32_t allocateChair() {
        Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);

        // Check if we have chairs available
        if (state_->chairPool.chairsInUse >= Config::Chair::MAX_CONCURRENT_IN_USE) {
            return -1;
        }

        // Find an available chair
        for (uint32_t i = 0; i < Config::Chair::QUANTITY; ++i) {
            uint32_t chairIdx = (nextChairId_ + i) % Config::Chair::QUANTITY;
            if (!state_->chairPool.chairs[chairIdx].isOccupied) {
                state_->chairPool.chairs[chairIdx].isOccupied = true;
                state_->chairPool.chairs[chairIdx].numPassengers = 0;
                state_->chairPool.chairs[chairIdx].slotsUsed = 0;
                state_->chairPool.chairsInUse++;
                nextChairId_ = (chairIdx + 1) % Config::Chair::QUANTITY;
                return static_cast<int32_t>(chairIdx);
            }
        }

        return -1;
    }

    /**
     * Board passengers onto a chair
     */
    bool boardPassengers(uint32_t chairId, const PassengerGroup& group) {
        if (chairId >= Config::Chair::QUANTITY) {
            return false;
        }

        Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);

        Chair& chair = state_->chairPool.chairs[chairId];
        chair.numPassengers = static_cast<uint32_t>(group.touristIds.size());
        chair.slotsUsed = group.slotsUsed;
        chair.departureTime = time(nullptr);
        chair.arrivalTime = chair.departureTime + Config::Chair::RIDE_DURATION_US / Config::Time::ONE_SECOND_US;

        for (size_t i = 0; i < group.touristIds.size() && i < 4; ++i) {
            chair.passengerIds[i] = static_cast<int32_t>(group.touristIds[i]);
        }

        std::cout << "[RideGate] Chair " << chairId << " departing with "
                  << group.touristIds.size() << " passengers ("
                  << group.slotsUsed << " slots used)" << std::endl;

        return true;
    }

    /**
     * Release a chair when passengers disembark
     */
    void releaseChair(uint32_t chairId) {
        if (chairId >= Config::Chair::QUANTITY) {
            return;
        }

        Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);

        Chair& chair = state_->chairPool.chairs[chairId];
        chair.isOccupied = false;
        chair.numPassengers = 0;
        chair.slotsUsed = 0;
        for (int i = 0; i < 4; ++i) {
            chair.passengerIds[i] = -1;
        }

        if (state_->chairPool.chairsInUse > 0) {
            state_->chairPool.chairsInUse--;
        }
    }

    /**
     * Get gate number for a tourist (round-robin)
     */
    uint32_t getGateNumber(uint32_t touristId) {
        return touristId % Config::Gate::NUM_RIDE_GATES;
    }

    /**
     * Check if boarding is currently allowed
     */
    bool isBoardingAllowed() {
        Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
        return state_->core.state == RopewayState::RUNNING;
    }

private:
    Semaphore& sem_;
    RopewaySystemState* state_;
    uint32_t nextChairId_;
};
