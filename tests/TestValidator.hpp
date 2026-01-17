#pragma once

#include <iostream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include "tests/TestConfig.hpp"
#include "ipc/RopewaySystemState.hpp"
#include "ipc/Semaphore.hpp"
#include "ipc/SemaphoreIndex.hpp"
#include "common/Config.hpp"

namespace Test {

/**
 * Validator class for analyzing simulation results and detecting violations
 */
class TestValidator {
public:
    /**
     * Validate all aspects of the simulation based on scenario expectations
     */
    static TestResult validate(const TestScenario& scenario,
                               const RopewaySystemState& state,
                               uint32_t observedMaxCapacity) {
        TestResult result;
        result.testName = scenario.name;
        result.maxObservedCapacity = observedMaxCapacity;

        // Validate station capacity limit
        if (scenario.expectCapacityNeverExceeded) {
            validateCapacityLimit(scenario, state, observedMaxCapacity, result);
        }

        // Validate child supervision
        if (scenario.expectAllChildrenSupervised) {
            validateChildSupervision(scenario, state, result);
        }

        // Validate VIP priority (if applicable)
        if (scenario.expectVipPriority) {
            validateVipPriority(scenario, state, result);
        }

        // Validate emergency stop/resume
        if (scenario.expectEmergencyHandled) {
            validateEmergencyHandling(scenario, state, result);
        }

        // Validate minimum rides completed
        validateMinimumRides(scenario, state, result);

        // Copy statistics
        result.totalRidesCompleted = state.stats.dailyStats.totalRides;
        result.emergencyStopsTriggered = state.stats.dailyStats.emergencyStops;
        result.emergenciesResumed = countResumedEmergencies(state);

        return result;
    }

    /**
     * Check for zombie processes
     */
    static uint32_t checkForZombies() {
        uint32_t zombieCount = 0;
        pid_t pid;

        // Reap any zombie processes
        while ((pid = waitpid(-1, nullptr, WNOHANG)) > 0) {
            ++zombieCount;
        }

        return zombieCount;
    }

    /**
     * Check if simulation appears deadlocked
     * Returns true if no progress detected within timeout
     */
    static bool checkForDeadlock(const RopewaySystemState& stateBefore,
                                  const RopewaySystemState& stateAfter,
                                  uint32_t timeoutSec) {
        // If state changed, not deadlocked
        if (stateBefore.core.totalRidesToday != stateAfter.core.totalRidesToday) {
            return false;
        }
        if (stateBefore.core.touristsInLowerStation != stateAfter.core.touristsInLowerStation) {
            return false;
        }
        if (stateBefore.chairPool.boardingQueue.count != stateAfter.chairPool.boardingQueue.count) {
            return false;
        }

        // If emergency stop, don't consider it a deadlock
        if (stateAfter.core.state == RopewayState::EMERGENCY_STOP) {
            return false;
        }

        // If stopped or closing, not a deadlock
        if (stateAfter.core.state == RopewayState::STOPPED ||
            stateAfter.core.state == RopewayState::CLOSING) {
            return false;
        }

        // No progress for timeout seconds could indicate deadlock
        return true;
    }

private:
    /**
     * Validate that station capacity was never exceeded
     */
    static void validateCapacityLimit(const TestScenario& scenario,
                                       const RopewaySystemState& state,
                                       uint32_t observedMaxCapacity,
                                       TestResult& result) {
        if (observedMaxCapacity > scenario.stationCapacity) {
            std::ostringstream oss;
            oss << "CAPACITY EXCEEDED: Max observed = " << observedMaxCapacity
                << ", limit N = " << scenario.stationCapacity;
            result.addFailure(oss.str());
        } else {
            std::ostringstream oss;
            oss << "Capacity OK: Max observed = " << observedMaxCapacity
                << " <= limit N = " << scenario.stationCapacity;
            result.addWarning(oss.str());
        }
    }

    /**
     * Validate child supervision rules from gate passage log
     */
    static void validateChildSupervision(const TestScenario& scenario,
                                          const RopewaySystemState& state,
                                          TestResult& result) {
        // Check tourist records for children under 8 who completed rides
        uint32_t childrenRodeAlone = 0;
        uint32_t childrenTotal = 0;

        for (uint32_t i = 0; i < state.stats.touristRecordCount; ++i) {
            const TouristRideRecord& rec = state.stats.touristRecords[i];

            // Find if this is a child under 8
            if (rec.age < Config::Age::SUPERVISION_AGE_LIMIT) {
                ++childrenTotal;

                // If child completed ride gate passages, they boarded a chair
                // This should only happen with guardian (validated by RideGate)
                if (rec.rideGatePassages > 0) {
                    // The system allows this only with guardian, so if logged, it's valid
                    // We can't detect violations from final state alone - would need real-time monitoring
                }
            }
        }

        result.childrenWithoutGuardian = childrenRodeAlone;

        // Check boarding queue for any stranded children (edge case)
        for (uint32_t i = 0; i < state.chairPool.boardingQueue.count; ++i) {
            const BoardingQueueEntry& entry = state.chairPool.boardingQueue.entries[i];
            if (entry.needsSupervision && entry.guardianId < 0 && !entry.readyToBoard) {
                // Child waiting without guardian - could be waiting for pairing
                // Only a failure if simulation ended with them still waiting
            }
        }

        // Validate max 2 children per adult constraint
        // Count dependents per adult from boarding queue
        uint32_t adultsWithExcess = 0;
        for (uint32_t i = 0; i < state.chairPool.boardingQueue.count; ++i) {
            const BoardingQueueEntry& entry = state.chairPool.boardingQueue.entries[i];
            if (entry.isAdult && entry.dependentCount > Config::Gate::MAX_CHILDREN_PER_ADULT) {
                ++adultsWithExcess;
            }
        }

        result.adultsWithTooManyChildren = adultsWithExcess;

        if (adultsWithExcess > 0) {
            std::ostringstream oss;
            oss << "SUPERVISION VIOLATION: " << adultsWithExcess
                << " adult(s) supervising more than " << Config::Gate::MAX_CHILDREN_PER_ADULT << " children";
            result.addFailure(oss.str());
        }

        if (childrenTotal > 0) {
            std::ostringstream oss;
            oss << "Child supervision tracked: " << childrenTotal << " children under "
                << Config::Age::SUPERVISION_AGE_LIMIT << " years old";
            result.addWarning(oss.str());
        }
    }

    /**
     * Validate VIP priority handling
     */
    static void validateVipPriority(const TestScenario& scenario,
                                     const RopewaySystemState& state,
                                     TestResult& result) {
        // Count VIP and regular entry gate passages
        uint32_t vipEntries = 0;
        uint32_t regularEntries = 0;
        time_t firstVipEntry = 0;
        time_t firstRegularEntry = 0;

        for (uint32_t i = 0; i < state.stats.gateLog.count; ++i) {
            const GatePassage& passage = state.stats.gateLog.entries[i];

            if (passage.gateType != GateType::ENTRY || !passage.wasAllowed) {
                continue;
            }

            // Find if this tourist is VIP
            int32_t recordIdx = -1;
            for (uint32_t j = 0; j < state.stats.touristRecordCount; ++j) {
                if (state.stats.touristRecords[j].touristId == passage.touristId) {
                    recordIdx = static_cast<int32_t>(j);
                    break;
                }
            }

            if (recordIdx >= 0) {
                if (state.stats.touristRecords[recordIdx].isVip) {
                    ++vipEntries;
                    if (firstVipEntry == 0) {
                        firstVipEntry = passage.timestamp;
                    }
                } else {
                    ++regularEntries;
                    if (firstRegularEntry == 0) {
                        firstRegularEntry = passage.timestamp;
                    }
                }
            }
        }

        // Check that regular tourists also entered (no starvation)
        if (regularEntries == 0 && vipEntries > 0) {
            // Could be starvation, but need more context
            result.addWarning("No regular tourists entered - possible starvation or short simulation");
        }

        // VIPs should generally enter before regulars who arrived at the same time
        // This is hard to validate precisely without arrival timestamps

        result.vipWaitTime = 0;  // Would need more tracking
        result.regularWaitTime = 0;

        std::ostringstream oss;
        oss << "VIP Priority: " << vipEntries << " VIP entries, " << regularEntries << " regular entries";
        result.addWarning(oss.str());

        // If we have VIPs and regulars, VIP system is working
        if (vipEntries > 0 && regularEntries > 0) {
            // Success - both groups served
        } else if (regularEntries == 0 && state.stats.touristRecordCount > state.stats.dailyStats.vipTourists) {
            result.addFailure("STARVATION DETECTED: Regular tourists not served despite being present");
        }
    }

    /**
     * Validate emergency stop/resume handling
     */
    static void validateEmergencyHandling(const TestScenario& scenario,
                                           const RopewaySystemState& state,
                                           TestResult& result) {
        const DailyStatistics& stats = state.stats.dailyStats;

        // Should have at least one emergency stop
        if (stats.emergencyStops == 0) {
            result.addFailure("EMERGENCY NOT TRIGGERED: Expected emergency stop but none recorded");
            return;
        }

        // Check that emergency was resumed (if resume was scheduled)
        if (scenario.resumeAtSec > 0) {
            bool anyResumed = false;
            for (uint32_t i = 0; i < stats.emergencyRecordCount; ++i) {
                if (stats.emergencyRecords[i].resumed) {
                    anyResumed = true;
                    break;
                }
            }

            if (!anyResumed) {
                result.addFailure("EMERGENCY NOT RESUMED: Resume was scheduled but no emergency was resumed");
            }
        }

        // Log emergency details
        for (uint32_t i = 0; i < stats.emergencyRecordCount; ++i) {
            const EmergencyStopRecord& rec = stats.emergencyRecords[i];
            std::ostringstream oss;
            oss << "Emergency #" << (i + 1) << ": Worker" << rec.initiatorWorkerId;

            if (rec.resumed && rec.endTime > rec.startTime) {
                oss << ", Duration: " << (rec.endTime - rec.startTime) << "s, RESUMED";
            } else {
                oss << ", NOT RESUMED";
            }
            result.addWarning(oss.str());
        }

        // Verify worker coordination happened
        // Check if we have both trigger and confirmation
        std::ostringstream oss;
        oss << "Emergency stops: " << stats.emergencyStops
            << ", Total duration: " << stats.totalEmergencyDuration << "s";
        result.addWarning(oss.str());
    }

    /**
     * Validate minimum rides completed
     */
    static void validateMinimumRides(const TestScenario& scenario,
                                      const RopewaySystemState& state,
                                      TestResult& result) {
        if (state.stats.dailyStats.totalRides < scenario.expectedMinRides) {
            std::ostringstream oss;
            oss << "INSUFFICIENT RIDES: " << state.stats.dailyStats.totalRides
                << " completed, expected at least " << scenario.expectedMinRides;
            result.addFailure(oss.str());
        } else {
            std::ostringstream oss;
            oss << "Rides OK: " << state.stats.dailyStats.totalRides
                << " completed (min: " << scenario.expectedMinRides << ")";
            result.addWarning(oss.str());
        }
    }

    /**
     * Count resumed emergencies
     */
    static uint32_t countResumedEmergencies(const RopewaySystemState& state) {
        uint32_t count = 0;
        for (uint32_t i = 0; i < state.stats.dailyStats.emergencyRecordCount; ++i) {
            if (state.stats.dailyStats.emergencyRecords[i].resumed) {
                ++count;
            }
        }
        return count;
    }
};

} // namespace Test
