#pragma once

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

#include "tests/TestConfig.hpp"
#include "tests/TestValidator.hpp"
#include "ipc/IpcManager.hpp"
#include "utils/SignalHelper.hpp"
#include "utils/ProcessSpawner.hpp"

namespace Test {

class TestRunner {
public:
    static constexpr int KEY_OFFSET = 100;

    TestRunner() : keyOffset_{KEY_OFFSET} {
        SignalHelper::setup(signals_, SignalHelper::Mode::ORCHESTRATOR);
    }

    std::vector<TestResult> runAllTests() {
        std::vector<TestResult> results;

        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "           ROPEWAY SIMULATION - AUTOMATED TEST SUITE\n";
        std::cout << std::string(70, '=') << "\n\n";

        results.push_back(runTest(Scenarios::createCapacityLimitTest()));
        results.push_back(runTest(Scenarios::createChildSupervisionTest()));
        results.push_back(runTest(Scenarios::createVipPriorityTest()));
        results.push_back(runTest(Scenarios::createEmergencyStopTest()));

        printSummary(results);
        return results;
    }

    TestResult runTest(const TestScenario& scenario) {
        std::cout << "\n" << std::string(60, '-') << "\n";
        std::cout << "Running: " << scenario.name << "\n";
        std::cout << "Description: " << scenario.description << "\n";
        std::cout << std::string(60, '-') << "\n";

        TestResult result;
        result.testName = scenario.name;

        try {
            IpcManager::cleanup(Config::Ipc::SHM_KEY_BASE, keyOffset_);

            IpcManager ipc(Config::Ipc::SHM_KEY_BASE, true, keyOffset_);
            ipc.initializeSemaphores(scenario.stationCapacity);

            time_t startTime = time(nullptr);
            ipc.initializeState(startTime, startTime + scenario.simulationDurationSec + 10);

            std::cout << "[Test] Station capacity N = " << scenario.stationCapacity << "\n";
            std::cout << "[Test] Simulation duration: " << scenario.simulationDurationSec << "s\n";
            std::cout << "[Test] Tourists: " << scenario.tourists.size() << "\n";

            pid_t cashierPid = ProcessSpawner::spawnWithKeys("cashier_process",
                ipc.shmKey(), ipc.semKey(), ipc.cashierMsgKey());
            usleep(100000);

            pid_t worker1Pid = ProcessSpawner::spawnWithKeys("worker1_process",
                ipc.shmKey(), ipc.semKey(), ipc.msgKey());
            pid_t worker2Pid = ProcessSpawner::spawnWithKeys("worker2_process",
                ipc.shmKey(), ipc.semKey(), ipc.msgKey());

            {
                SemaphoreLock lock(ipc.semaphores(), SemaphoreIndex::SHARED_MEMORY);
                ipc.state()->worker1Pid = worker1Pid;
                ipc.state()->worker2Pid = worker2Pid;
            }
            usleep(200000);

            std::cout << "[Test] Spawning " << scenario.tourists.size() << " tourists...\n";

            std::vector<pid_t> touristPids;
            for (const auto& t : scenario.tourists) {
                usleep(t.spawnDelayMs * 1000);
                pid_t pid = spawnTourist(t, ipc);
                if (pid > 0) {
                    touristPids.push_back(pid);
                }
            }

            result = runSimulationLoop(scenario, ipc, worker1Pid);

            ProcessSpawner::terminate(cashierPid, "Cashier");
            ProcessSpawner::terminate(worker1Pid, "Worker1");
            ProcessSpawner::terminate(worker2Pid, "Worker2");
            ProcessSpawner::terminateAll(touristPids);

            usleep(300000);

            result.zombieProcesses = TestValidator::checkForZombies();
            if (result.zombieProcesses > 0 && scenario.expectNoZombies) {
                std::ostringstream oss;
                oss << "ZOMBIES DETECTED: " << result.zombieProcesses << " zombie process(es)";
                result.addFailure(oss.str());
            }

            ProcessSpawner::waitForAll();
            result.simulationDuration = time(nullptr) - startTime;

        } catch (const std::exception& e) {
            result.addFailure(std::string("EXCEPTION: ") + e.what());
        }

        IpcManager::cleanup(Config::Ipc::SHM_KEY_BASE, keyOffset_);
        printTestResult(result);
        return result;
    }

private:
    int keyOffset_;
    SignalHelper::SignalFlags signals_;

    pid_t spawnTourist(const TouristTestConfig& t, IpcManager& ipc) {
        char idStr[16], ageStr[16], typeStr[16], vipStr[16], rideStr[16];
        char guardianStr[16], trailStr[16], shmStr[16], semStr[16], msgStr[16], cashierMsgStr[16];

        snprintf(idStr, sizeof(idStr), "%u", t.id);
        snprintf(ageStr, sizeof(ageStr), "%u", t.age);
        snprintf(typeStr, sizeof(typeStr), "%d", static_cast<int>(t.type));
        snprintf(vipStr, sizeof(vipStr), "%d", t.requestVip ? 1 : 0);
        snprintf(rideStr, sizeof(rideStr), "%d", t.wantsToRide ? 1 : 0);
        snprintf(guardianStr, sizeof(guardianStr), "%d", t.guardianId);
        snprintf(trailStr, sizeof(trailStr), "%d", static_cast<int>(t.trail));
        snprintf(shmStr, sizeof(shmStr), "%d", ipc.shmKey());
        snprintf(semStr, sizeof(semStr), "%d", ipc.semKey());
        snprintf(msgStr, sizeof(msgStr), "%d", ipc.msgKey());
        snprintf(cashierMsgStr, sizeof(cashierMsgStr), "%d", ipc.cashierMsgKey());

        return ProcessSpawner::spawn("tourist_process", {
            idStr, ageStr, typeStr, vipStr, rideStr,
            guardianStr, trailStr, shmStr, semStr, msgStr, cashierMsgStr
        });
    }

    TestResult runSimulationLoop(const TestScenario& scenario, IpcManager& ipc, pid_t worker1Pid) {
        TestResult result;
        result.testName = scenario.name;

        time_t startTime = time(nullptr);
        bool emergencyTriggered = false;
        bool resumeTriggered = false;
        uint32_t maxObservedCapacity = 0;
        RopewaySystemState lastState{};

        std::cout << "[Test] Simulation running..." << std::endl;

        while (!SignalHelper::shouldExit(signals_)) {
            time_t elapsed = time(nullptr) - startTime;

            {
                SemaphoreLock lock(ipc.semaphores(), SemaphoreIndex::SHARED_MEMORY);
                uint32_t currentCapacity = ipc.state()->touristsInLowerStation;
                if (currentCapacity > maxObservedCapacity) {
                    maxObservedCapacity = currentCapacity;
                }

                if (ipc.state()->state == RopewayState::STOPPED) {
                    std::cout << "[Test] Ropeway stopped.\n";
                    break;
                }

                if (elapsed > 0 && elapsed % 5 == 0) {
                    if (TestValidator::checkForDeadlock(lastState, *ipc.state(), 5)) {
                        result.addWarning("Possible deadlock detected - no progress for 5 seconds");
                    }
                    lastState = *ipc.state();
                }
            }

            if (scenario.emergencyStopAtSec > 0 &&
                elapsed >= scenario.emergencyStopAtSec && !emergencyTriggered) {
                std::cout << "[Test] >>> TRIGGERING EMERGENCY STOP <<<\n";
                if (worker1Pid > 0) {
                    kill(worker1Pid, SIGUSR1);
                }
                emergencyTriggered = true;
            }

            if (scenario.resumeAtSec > 0 &&
                elapsed >= scenario.resumeAtSec && emergencyTriggered && !resumeTriggered) {
                std::cout << "[Test] >>> TRIGGERING RESUME <<<\n";
                if (worker1Pid > 0) {
                    kill(worker1Pid, SIGUSR2);
                }
                resumeTriggered = true;
            }

            if (elapsed >= scenario.simulationDurationSec) {
                std::cout << "[Test] Simulation timeout reached.\n";
                break;
            }

            usleep(200000);
        }

        {
            SemaphoreLock lock(ipc.semaphores(), SemaphoreIndex::SHARED_MEMORY);
            ipc.state()->dailyStats.simulationEndTime = time(nullptr);
            result = TestValidator::validate(scenario, *ipc.state(), maxObservedCapacity);
        }

        return result;
    }

    void printTestResult(const TestResult& result) {
        std::cout << "\n";

        if (result.passed) {
            std::cout << "[PASSED] " << result.testName << "\n";
        } else {
            std::cout << "[FAILED] " << result.testName << "\n";
        }

        for (const auto& failure : result.failures) {
            std::cout << "  [FAIL] " << failure << "\n";
        }

        uint32_t warnCount = 0;
        for (const auto& warning : result.warnings) {
            std::cout << "  [INFO] " << warning << "\n";
            if (++warnCount >= 5) {
                std::cout << "  ... (" << (result.warnings.size() - 5) << " more)\n";
                break;
            }
        }

        std::cout << "  Metrics:\n";
        std::cout << "    - Max capacity observed: " << result.maxObservedCapacity << "\n";
        std::cout << "    - Total rides completed: " << result.totalRidesCompleted << "\n";
        std::cout << "    - Emergency stops: " << result.emergencyStopsTriggered << "\n";
        std::cout << "    - Emergencies resumed: " << result.emergenciesResumed << "\n";
        std::cout << "    - Zombie processes: " << result.zombieProcesses << "\n";
        std::cout << "    - Simulation duration: " << result.simulationDuration << "s\n";
    }

    void printSummary(const std::vector<TestResult>& results) {
        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "                        TEST SUMMARY\n";
        std::cout << std::string(70, '=') << "\n\n";

        uint32_t passed = 0;
        uint32_t failed = 0;

        for (const auto& r : results) {
            std::cout << "  " << std::left << std::setw(35) << r.testName
                      << (r.passed ? "[PASSED]" : "[FAILED]") << "\n";
            if (r.passed) ++passed;
            else ++failed;
        }

        std::cout << "\n" << std::string(50, '-') << "\n";
        std::cout << "  Total: " << results.size() << " tests, "
                  << passed << " passed, " << failed << " failed\n";

        if (failed == 0) {
            std::cout << "\n  ALL TESTS PASSED!\n";
        } else {
            std::cout << "\n  SOME TESTS FAILED - Review output above\n";
        }

        std::cout << std::string(70, '=') << "\n\n";
    }
};

}
