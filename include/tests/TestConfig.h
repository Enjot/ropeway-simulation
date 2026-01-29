#pragma once

#include <cstdlib>
#include <string>
#include <vector>
#include "tourist/TouristType.h"
#include "ropeway/TrailDifficulty.h"

namespace Test {
    /**
     * Test environment configuration.
     * Sets env vars to override Config values for testing.
     */
    struct TestEnvConfig {
        uint32_t stationCapacity = 20;
        uint32_t timeScale = 600;
        uint32_t openingHour = 8;
        uint32_t closingHour = 18;
        uint32_t mainLoopPollUs = 100000; // Faster polling for tests
        uint32_t arrivalDelayBaseUs = 1000;
        uint32_t arrivalDelayRandomUs = 2000;
        uint32_t rideDurationUs = 500000; // Faster rides for tests
        uint32_t trailEasyUs = 500000;
        uint32_t trailMediumUs = 1000000;
        uint32_t trailHardUs = 1500000;
        uint32_t tk1DurationSec = 6;
        uint32_t tk2DurationSec = 12;
        uint32_t tk3DurationSec = 24;
        uint32_t dailyDurationSec = 60;
        uint32_t ticketSingleUsePct = 40;
        uint32_t ticketTk1Pct = 20;
        uint32_t ticketTk2Pct = 15;
        uint32_t ticketTk3Pct = 15;

        void apply() const {
            setenv("ROPEWAY_STATION_CAPACITY", std::to_string(stationCapacity).c_str(), 1);
            setenv("ROPEWAY_TIME_SCALE", std::to_string(timeScale).c_str(), 1);
            setenv("ROPEWAY_OPENING_HOUR", std::to_string(openingHour).c_str(), 1);
            setenv("ROPEWAY_CLOSING_HOUR", std::to_string(closingHour).c_str(), 1);
            setenv("ROPEWAY_MAIN_LOOP_POLL_US", std::to_string(mainLoopPollUs).c_str(), 1);
            setenv("ROPEWAY_ARRIVAL_DELAY_BASE_US", std::to_string(arrivalDelayBaseUs).c_str(), 1);
            setenv("ROPEWAY_ARRIVAL_DELAY_RANDOM_US", std::to_string(arrivalDelayRandomUs).c_str(), 1);
            setenv("ROPEWAY_RIDE_DURATION_US", std::to_string(rideDurationUs).c_str(), 1);
            setenv("ROPEWAY_TRAIL_EASY_US", std::to_string(trailEasyUs).c_str(), 1);
            setenv("ROPEWAY_TRAIL_MEDIUM_US", std::to_string(trailMediumUs).c_str(), 1);
            setenv("ROPEWAY_TRAIL_HARD_US", std::to_string(trailHardUs).c_str(), 1);
            setenv("ROPEWAY_TK1_DURATION_SEC", std::to_string(tk1DurationSec).c_str(), 1);
            setenv("ROPEWAY_TK2_DURATION_SEC", std::to_string(tk2DurationSec).c_str(), 1);
            setenv("ROPEWAY_TK3_DURATION_SEC", std::to_string(tk3DurationSec).c_str(), 1);
            setenv("ROPEWAY_DAILY_DURATION_SEC", std::to_string(dailyDurationSec).c_str(), 1);
            setenv("ROPEWAY_TICKET_SINGLE_USE_PCT", std::to_string(ticketSingleUsePct).c_str(), 1);
            setenv("ROPEWAY_TICKET_TK1_PCT", std::to_string(ticketTk1Pct).c_str(), 1);
            setenv("ROPEWAY_TICKET_TK2_PCT", std::to_string(ticketTk2Pct).c_str(), 1);
            setenv("ROPEWAY_TICKET_TK3_PCT", std::to_string(ticketTk3Pct).c_str(), 1);
            // NUM_TOURISTS and DURATION_US not set - tests manage tourists directly
            setenv("ROPEWAY_NUM_TOURISTS", "0", 1);
            setenv("ROPEWAY_DURATION_US", "999999999", 1);
        }
    };

    /**
     * Configuration for a single tourist in a test scenario
     */
    struct TouristTestConfig {
        uint32_t id;
        uint32_t age;
        TouristType type;
        bool requestVip;
        bool wantsToRide;
        int32_t guardianId;
        TrailDifficulty trail;
        uint32_t spawnDelayMs; // Delay before spawning this tourist
        uint32_t numChildren; // Number of children this adult will spawn (0-2)

        TouristTestConfig(uint32_t id_, uint32_t age_, TouristType type_ = TouristType::PEDESTRIAN,
                          bool vip_ = false, bool ride_ = true, int32_t guardian_ = -1,
                          TrailDifficulty trail_ = TrailDifficulty::EASY, uint32_t delay_ = 100,
                          uint32_t numChildren_ = 0)
            : id{id_}, age{age_}, type{type_}, requestVip{vip_}, wantsToRide{ride_},
              guardianId{guardian_}, trail{trail_}, spawnDelayMs{delay_}, numChildren{numChildren_} {
        }
    };

    /**
     * Test scenario configuration
     */
    struct TestScenario {
        std::string name;
        std::string description;
        uint32_t simulationDurationSec; // Total simulation time
        uint32_t emergencyStopAtSec; // When to trigger emergency (0 = don't trigger)
        uint32_t resumeAtSec; // When to resume after emergency (0 = don't resume)
        std::vector<TouristTestConfig> tourists;
        TestEnvConfig env; // Environment configuration for this test

        // Expected outcomes for validation
        bool expectCapacityNeverExceeded;
        bool expectAllChildrenSupervised;
        bool expectVipPriority;
        bool expectEmergencyHandled;
        bool expectNoZombies;
        bool expectNoDeadlocks;
        uint32_t expectedMinRides; // Minimum rides to consider test valid

        uint32_t stationCapacity() const { return env.stationCapacity; }
    };

    /**
     * Test result structure
     */
    struct TestResult {
        std::string testName;
        bool passed;
        std::vector<std::string> failures;
        std::vector<std::string> warnings;

        // Metrics collected
        uint32_t maxObservedCapacity;
        uint32_t childrenWithoutGuardian;
        uint32_t adultsWithTooManyChildren;
        uint32_t vipWaitTime;
        uint32_t regularWaitTime;
        uint32_t emergencyStopsTriggered;
        uint32_t emergenciesResumed;
        uint32_t zombieProcesses;
        uint32_t totalRidesCompleted;
        time_t simulationDuration;

        TestResult() : passed{true}, maxObservedCapacity{0}, childrenWithoutGuardian{0},
                       adultsWithTooManyChildren{0}, vipWaitTime{0}, regularWaitTime{0},
                       emergencyStopsTriggered{0}, emergenciesResumed{0}, zombieProcesses{0},
                       totalRidesCompleted{0}, simulationDuration{0} {
        }

        void addFailure(const std::string &msg) {
            failures.push_back(msg);
            passed = false;
        }

        void addWarning(const std::string &msg) {
            warnings.push_back(msg);
        }
    };

    /**
     * Pre-configured test scenarios matching requirements
     */
    namespace Scenarios {
        /**
         * Test 1: Station Capacity Limit (N)
         * Parameters: N = 5, tourists = 30, simulation = 60s
         * Validates: Capacity never exceeds N, no deadlocks, no zombies
         */
        inline TestScenario createCapacityLimitTest() {
            TestScenario scenario;
            scenario.name = "Test1_StationCapacityLimit";
            scenario.description = "Verify station capacity N is never exceeded";
            scenario.env.stationCapacity = 5;
            scenario.simulationDurationSec = 60;
            scenario.emergencyStopAtSec = 0;
            scenario.resumeAtSec = 0;

            // Generate 30 tourists with staggered arrivals (as per specification)
            for (uint32_t i = 1; i <= 30; ++i) {
                uint32_t age = 20 + (i % 40); // Ages 20-59
                TouristType type = (i % 3 == 0) ? TouristType::CYCLIST : TouristType::PEDESTRIAN;
                TrailDifficulty trail = static_cast<TrailDifficulty>(i % 3);
                scenario.tourists.emplace_back(i, age, type, false, true, -1, trail, 500);
            }

            scenario.expectCapacityNeverExceeded = true;
            scenario.expectAllChildrenSupervised = true;
            scenario.expectVipPriority = false;
            scenario.expectEmergencyHandled = false;
            scenario.expectNoZombies = true;
            scenario.expectNoDeadlocks = true;
            scenario.expectedMinRides = 10;

            return scenario;
        }

        /**
         * Test 2: Child Supervision
         * Parameters: 20 tourists (6 children <8, 3 adults, rest others), simulation = 90s
         * Validates: Children always with guardian, max 2 children per adult
         */
        inline TestScenario createChildSupervisionTest() {
            TestScenario scenario;
            scenario.name = "Test2_ChildSupervision";
            scenario.description = "Verify children under 8 always have guardian, max 2 per adult";
            scenario.env.stationCapacity = 15;
            scenario.simulationDurationSec = 90;
            scenario.emergencyStopAtSec = 0;
            scenario.resumeAtSec = 0;

            uint32_t id = 1;

            // 3 adults (guardians) - each will have 2 children spawned via numChildren parameter
            // Adult 1 (id=1): will spawn children 4 and 5
            scenario.tourists.emplace_back(id++, 35, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY,
                                           100, 2);
            // Adult 2 (id=2): will spawn children 6 and 7
            scenario.tourists.emplace_back(id++, 40, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY,
                                           100, 2);
            // Adult 3 (id=3): will spawn children 8 and 9
            scenario.tourists.emplace_back(id++, 45, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY,
                                           100, 2);

            // Skip IDs 4-9 as they will be spawned by parents
            id = 10;

            // 11 other tourists (no children, various ages)
            for (uint32_t i = 0; i < 11; ++i) {
                uint32_t age = 15 + (i * 5); // 15, 20, 25, ... 65
                TouristType type = (i % 2 == 0) ? TouristType::PEDESTRIAN : TouristType::CYCLIST;
                scenario.tourists.emplace_back(id++, age, type, false, true, -1, TrailDifficulty::EASY, 500);
            }

            scenario.expectCapacityNeverExceeded = true;
            scenario.expectAllChildrenSupervised = true;
            scenario.expectVipPriority = false;
            scenario.expectEmergencyHandled = false;
            scenario.expectNoZombies = true;
            scenario.expectNoDeadlocks = true;
            scenario.expectedMinRides = 5;

            return scenario;
        }

        /**
         * Test 3: VIP Priority
         * Parameters: 100 tourists (10 VIP = 10%), N = 15, simulation = 120s
         * Validates: VIPs enter faster, no starvation of regular tourists
         */
        inline TestScenario createVipPriorityTest() {
            TestScenario scenario;
            scenario.name = "Test3_VipPriority";
            scenario.description = "Verify VIP priority without starvation of regular tourists";
            scenario.env.stationCapacity = 15;
            scenario.simulationDurationSec = 120;
            scenario.emergencyStopAtSec = 0;
            scenario.resumeAtSec = 0;

            // Generate 100 tourists: 10 VIP (10%), 90 regular (as per specification)
            for (uint32_t i = 1; i <= 100; ++i) {
                uint32_t age = 20 + (i % 45);
                TouristType type = (i % 4 == 0) ? TouristType::CYCLIST : TouristType::PEDESTRIAN;
                bool isVip = (i <= 10); // First 10 are VIP (10%)
                TrailDifficulty trail = static_cast<TrailDifficulty>(i % 3);
                scenario.tourists.emplace_back(i, age, type, isVip, true, -1, trail, 300);
            }

            scenario.expectCapacityNeverExceeded = true;
            scenario.expectAllChildrenSupervised = true;
            scenario.expectVipPriority = true;
            scenario.expectEmergencyHandled = false;
            scenario.expectNoZombies = true;
            scenario.expectNoDeadlocks = true;
            scenario.expectedMinRides = 30;

            return scenario;
        }

        /**
         * Test 4: Emergency Stop/Resume
         * Parameters: 20 tourists, simulation = 60s, emergency at 20s, resume at 30s
         * Validates: Emergency stops immediately, resume only after confirmation
         */
        inline TestScenario createEmergencyStopTest() {
            TestScenario scenario;
            scenario.name = "Test4_EmergencyStopResume";
            scenario.description = "Verify emergency stop/resume protocol with worker coordination";
            scenario.env.stationCapacity = 15;
            scenario.simulationDurationSec = 60;
            scenario.emergencyStopAtSec = 20;
            scenario.resumeAtSec = 30;

            // Generate 20 tourists (as per specification)
            for (uint32_t i = 1; i <= 20; ++i) {
                uint32_t age = 20 + (i % 40);
                TouristType type = (i % 3 == 0) ? TouristType::CYCLIST : TouristType::PEDESTRIAN;
                TrailDifficulty trail = static_cast<TrailDifficulty>(i % 3);
                scenario.tourists.emplace_back(i, age, type, false, true, -1, trail, 500);
            }

            scenario.expectCapacityNeverExceeded = true;
            scenario.expectAllChildrenSupervised = true;
            scenario.expectVipPriority = false;
            scenario.expectEmergencyHandled = true;
            scenario.expectNoZombies = true;
            scenario.expectNoDeadlocks = true;
            scenario.expectedMinRides = 5;

            return scenario;
        }

        /**
         * Stress Test: High Load with 1000 tourists
         * Tests message queue limits, VIP priority under load, and system stability
         */
        inline TestScenario createStressTest() {
            TestScenario scenario;
            scenario.name = "StressTest_HighLoad";
            scenario.description = "Stress test with 1000 tourists to test message queue limits and VIP priority";
            scenario.env.stationCapacity = 50;
            scenario.simulationDurationSec = 180; // 3 minutes
            scenario.emergencyStopAtSec = 0;
            scenario.resumeAtSec = 0;

            // Generate 1000 tourists: 10 VIP (1%), 990 regular
            for (uint32_t i = 1; i <= 1000; ++i) {
                uint32_t age = 18 + (i % 50); // Ages 18-67
                TouristType type = (i % 5 == 0) ? TouristType::CYCLIST : TouristType::PEDESTRIAN;
                bool isVip = (i <= 10); // First 10 are VIP (1%)
                TrailDifficulty trail = static_cast<TrailDifficulty>(i % 3);
                // Spawn tourists rapidly - 50ms apart
                scenario.tourists.emplace_back(i, age, type, isVip, true, -1, trail, 50);
            }

            scenario.expectCapacityNeverExceeded = true;
            scenario.expectAllChildrenSupervised = true;
            scenario.expectVipPriority = true;
            scenario.expectEmergencyHandled = false;
            scenario.expectNoZombies = true;
            scenario.expectNoDeadlocks = true;
            scenario.expectedMinRides = 100;

            return scenario;
        }

        /**
         * Stress Test: Message Queue Saturation
         * Tests what happens when message queues fill up
         */
        inline TestScenario createQueueSaturationTest() {
            TestScenario scenario;
            scenario.name = "StressTest_QueueSaturation";
            scenario.description = "Test message queue saturation with burst of tourists";
            scenario.env.stationCapacity = 20; // Moderate capacity
            scenario.simulationDurationSec = 90; // Longer duration for throughput
            scenario.emergencyStopAtSec = 0;
            scenario.resumeAtSec = 0;

            // Generate 200 tourists spawning rapidly (50ms apart)
            for (uint32_t i = 1; i <= 200; ++i) {
                uint32_t age = 20 + (i % 40);
                TouristType type = (i % 4 == 0) ? TouristType::CYCLIST : TouristType::PEDESTRIAN;
                bool isVip = (i % 20 == 0); // 5% VIP
                TrailDifficulty trail = static_cast<TrailDifficulty>(i % 3);
                scenario.tourists.emplace_back(i, age, type, isVip, true, -1, trail, 50);
            }

            scenario.expectCapacityNeverExceeded = true;
            scenario.expectAllChildrenSupervised = true;
            scenario.expectVipPriority = true;
            scenario.expectEmergencyHandled = false;
            scenario.expectNoZombies = true;
            scenario.expectNoDeadlocks = true;
            scenario.expectedMinRides = 15; // Realistic expectation given queue saturation

            return scenario;
        }
    } // namespace Scenarios
} // namespace Test