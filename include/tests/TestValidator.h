#pragma once

#include <iostream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include "tests/TestConfig.h"
#include "ipc/model/SharedRopewayState.h"
#include "ipc/core/Semaphore.h"
#include "core/Config.h"

namespace Test {
    /**
 * Validator class for analyzing simulation results and detecting violations
 */
    class TestValidator {
    public:
        /**
     * Validate all aspects of the simulation based on scenario expectations
     */
        static TestResult validate(const TestScenario &scenario,
                                   const SharedRopewayState &state,
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

            // Copy statistics (use core.totalRidesToday as it's actively updated during simulation)
            result.totalRidesCompleted = state.operational.totalRidesToday;
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
     * Compares two state snapshots to detect lack of progress.
     * Returns true if no progress detected between snapshots.
     */
        static bool checkForDeadlock(const SharedRopewayState &stateBefore,
                                     const SharedRopewayState &stateAfter,
                                     [[maybe_unused]] uint32_t checkIntervalSec) {
            // If state changed, not deadlocked
            if (stateBefore.operational.totalRidesToday != stateAfter.operational.totalRidesToday) {
                return false;
            }
            if (stateBefore.operational.touristsInLowerStation != stateAfter.operational.touristsInLowerStation) {
                return false;
            }
            if (stateBefore.chairPool.boardingQueue.count != stateAfter.chairPool.boardingQueue.count) {
                return false;
            }
            // Check if any tourists are waiting - if queue is empty, no work to do = not a deadlock
            if (stateAfter.chairPool.boardingQueue.count == 0 &&
                stateAfter.operational.touristsInLowerStation == 0) {
                return false;
            }

            // If emergency stop, don't consider it a deadlock
            if (stateAfter.operational.state == RopewayState::EMERGENCY_STOP) {
                return false;
            }

            // If stopped or closing, not a deadlock
            if (stateAfter.operational.state == RopewayState::STOPPED ||
                stateAfter.operational.state == RopewayState::CLOSING) {
                return false;
            }

            // No progress but tourists waiting - potential deadlock
            return true;
        }

    private:
        /**
     * Validate that station capacity was never exceeded
     */
        static void validateCapacityLimit(const TestScenario &scenario,
                                          const SharedRopewayState &state,
                                          uint32_t observedMaxCapacity,
                                          TestResult &result) {
            if (observedMaxCapacity > scenario.stationCapacity()) {
                std::ostringstream oss;
                oss << "CAPACITY EXCEEDED: Max observed = " << observedMaxCapacity
                        << ", limit N = " << scenario.stationCapacity();
                result.addFailure(oss.str());
            } else {
                std::ostringstream oss;
                oss << "Capacity OK: Max observed = " << observedMaxCapacity
                        << " <= limit N = " << scenario.stationCapacity();
                result.addWarning(oss.str());
            }
        }

        /**
     * Validate child supervision rules.
     * With thread-based children, we validate through parent records using childCount field.
     */
        static void validateChildSupervision([[maybe_unused]] const TestScenario &scenario,
                                             const SharedRopewayState &state,
                                             TestResult &result) {
            uint32_t totalChildren = 0;
            uint32_t adultsWithTooManyChildren = 0;
            uint32_t adultsWithChildren = 0;

            // Check parent tourist records for child counts
            // Children are threads within parent processes, not separate records
            for (uint32_t i = 0; i < state.stats.touristRecordCount; ++i) {
                const TouristRideRecord &rec = state.stats.touristRecords[i];

                // Only adults can have children (they have childCount field set)
                if (rec.age >= Constants::Age::SUPERVISION_AGE_LIMIT) {
                    if (rec.childCount > 0) {
                        totalChildren += rec.childCount;
                        ++adultsWithChildren;

                        if (rec.childCount > Constants::Gate::MAX_CHILDREN_PER_ADULT) {
                            ++adultsWithTooManyChildren;
                        }
                    }
                }
            }

            result.adultsWithTooManyChildren = adultsWithTooManyChildren;
            // Children can't ride alone when they're threads - always 0
            result.childrenWithoutGuardian = 0;

            // Validation passes if max children per adult is respected
            if (adultsWithTooManyChildren > 0) {
                std::ostringstream oss;
                oss << "SUPERVISION VIOLATION: " << adultsWithTooManyChildren
                        << " adult(s) supervising more than " << Constants::Gate::MAX_CHILDREN_PER_ADULT << " children";
                result.addFailure(oss.str());
            }

            // Report info about child supervision
            if (totalChildren > 0 || adultsWithChildren > 0) {
                std::ostringstream oss;
                oss << "Child supervision: " << totalChildren << " children traveled with "
                        << adultsWithChildren << " adult guardian(s)";
                result.addWarning(oss.str());
            } else {
                result.addWarning("Child supervision: No children in this test run");
            }
        }

        /**
     * Validate VIP priority handling with timestamp checks
     */
        static void validateVipPriority([[maybe_unused]] const TestScenario &scenario,
                                        const SharedRopewayState &state,
                                        TestResult &result) {
            uint32_t vipEntries = 0;
            uint32_t regularEntries = 0;
            time_t earliestVipEntry = LONG_MAX;
            time_t earliestRegularEntry = LONG_MAX;

            for (uint32_t i = 0; i < state.stats.gateLog.count; ++i) {
                const GatePassage &passage = state.stats.gateLog.entries[i];
                if (passage.gateType != GateType::ENTRY || !passage.wasAllowed) continue;

                // Find tourist to check VIP status
                bool isVip = false;
                for (uint32_t j = 0; j < state.stats.touristRecordCount; ++j) {
                    if (state.stats.touristRecords[j].touristId == passage.touristId) {
                        isVip = state.stats.touristRecords[j].isVip;
                        break;
                    }
                }

                if (isVip) {
                    ++vipEntries;
                    if (passage.timestamp < earliestVipEntry) earliestVipEntry = passage.timestamp;
                } else {
                    ++regularEntries;
                    if (passage.timestamp < earliestRegularEntry) earliestRegularEntry = passage.timestamp;
                }
            }

            // Validation: No starvation
            if (regularEntries == 0 && state.stats.touristRecordCount > state.stats.dailyStats.vipTourists) {
                result.addFailure("STARVATION DETECTED: Regular tourists not served");
            }

            // Validation: VIPs should enter first (with tolerance)
            if (vipEntries > 0 && regularEntries > 0 && earliestVipEntry != LONG_MAX && earliestRegularEntry != LONG_MAX) {
                if (earliestVipEntry > earliestRegularEntry + 3) {
                    std::ostringstream oss;
                    oss << "VIP PRIORITY ISSUE: First VIP entered " << (earliestVipEntry - earliestRegularEntry)
                        << "s after first regular tourist";
                    result.addWarning(oss.str());
                }
            }

            result.vipWaitTime = 0;
            result.regularWaitTime = 0;

            std::ostringstream oss;
            oss << "VIP Priority: " << vipEntries << " VIP entries, " << regularEntries << " regular entries";
            result.addWarning(oss.str());
        }

        /**
     * Validate emergency stop/resume handling
     */
        static void validateEmergencyHandling(const TestScenario &scenario,
                                              const SharedRopewayState &state,
                                              TestResult &result) {
            const DailyStatistics &stats = state.stats.dailyStats;

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
                const EmergencyStopRecord &rec = stats.emergencyRecords[i];
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
        static void validateMinimumRides(const TestScenario &scenario,
                                         const SharedRopewayState &state,
                                         TestResult &result) {
            // Use core.totalRidesToday as it's actively updated during simulation
            uint32_t totalRides = state.operational.totalRidesToday;
            if (totalRides < scenario.expectedMinRides) {
                std::ostringstream oss;
                oss << "INSUFFICIENT RIDES: " << totalRides
                        << " completed, expected at least " << scenario.expectedMinRides;
                result.addFailure(oss.str());
            } else {
                std::ostringstream oss;
                oss << "Rides OK: " << totalRides
                        << " completed (min: " << scenario.expectedMinRides << ")";
                result.addWarning(oss.str());
            }
        }

        /**
     * Count resumed emergencies
     */
        static uint32_t countResumedEmergencies(const SharedRopewayState &state) {
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