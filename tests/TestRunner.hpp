#pragma once

#include <iostream>
#include <iomanip>
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
    TestRunner() {
        SignalHelper::setup(signals_, false);
        SignalHelper::ignoreChildren();
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
            IpcManager ipc;
            ipc.initSemaphores(scenario.stationCapacity);

            time_t startTime = time(nullptr);
            ipc.initState(startTime, startTime + scenario.simulationDurationSec + 10);

            std::cout << "[Test] Station capacity N = " << scenario.stationCapacity << "\n";
            std::cout << "[Test] Simulation duration: " << scenario.simulationDurationSec << "s\n";
            std::cout << "[Test] Tourists: " << scenario.tourists.size() << "\n";

            pid_t cashierPid = ProcessSpawner::spawnWithKeys("cashier_process",
                ipc.shmKey(), ipc.semKey(), ipc.cashierMsgKey());
            ipc.sem().wait(Semaphore::Index::CASHIER_READY);

            pid_t lowerWorkerPid = ProcessSpawner::spawnWithKeys("lower_worker_process",
                ipc.shmKey(), ipc.semKey(), ipc.workerMsgKey(), ipc.entryGateMsgKey());
            pid_t upperWorkerPid = ProcessSpawner::spawnWithKeys("upper_worker_process",
                ipc.shmKey(), ipc.semKey(), ipc.workerMsgKey(), ipc.entryGateMsgKey());

            {
                Semaphore::ScopedLock lock(ipc.sem(), Semaphore::Index::SHM_OPERATIONAL);
                ipc.state()->operational.lowerWorkerPid = lowerWorkerPid;
                ipc.state()->operational.upperWorkerPid = upperWorkerPid;
            }
            ipc.sem().wait(Semaphore::Index::LOWER_WORKER_READY);
            ipc.sem().wait(Semaphore::Index::UPPER_WORKER_READY);

            std::cout << "[Test] Spawning " << scenario.tourists.size() << " tourists...\n";

            std::vector<pid_t> touristPids;
            for (const auto& t : scenario.tourists) {
                usleep(t.spawnDelayMs * 1000);
                pid_t pid = spawnTourist(t, ipc);
                if (pid > 0) {
                    touristPids.push_back(pid);
                }
            }

            result = runSimulationLoop(scenario, ipc, lowerWorkerPid);

            ProcessSpawner::terminate(cashierPid, "Cashier");
            ProcessSpawner::terminate(lowerWorkerPid, "LowerWorker");
            ProcessSpawner::terminate(upperWorkerPid, "UpperWorker");
            ProcessSpawner::terminateAll(touristPids);

            // Brief pause to allow processes to become zombies if they haven't been reaped
            usleep(100000);  // 100ms

            // Check for zombie processes BEFORE reaping them
            result.zombieProcesses = TestValidator::checkForZombies();
            if (result.zombieProcesses > 0 && scenario.expectNoZombies) {
                std::ostringstream oss;
                oss << "ZOMBIES DETECTED: " << result.zombieProcesses << " zombie process(es)";
                result.addFailure(oss.str());
            }

            // Now wait for any remaining child processes
            ProcessSpawner::waitForAll();
            result.simulationDuration = time(nullptr) - startTime;

        } catch (const std::exception& e) {
            result.addFailure(std::string("EXCEPTION: ") + e.what());
        }

        printTestResult(result);
        return result;
    }

private:
    SignalHelper::Flags signals_;

    pid_t spawnTourist(const TouristTestConfig& t, IpcManager& ipc) {
        return ProcessSpawner::spawn("tourist_process", {
            std::to_string(t.id),
            std::to_string(t.age),
            std::to_string(static_cast<int>(t.type)),
            std::to_string(t.requestVip ? 1 : 0),
            std::to_string(t.wantsToRide ? 1 : 0),
            std::to_string(t.guardianId),
            std::to_string(t.numChildren),
            std::to_string(static_cast<int>(t.trail)),
            std::to_string(ipc.shmKey()),
            std::to_string(ipc.semKey()),
            std::to_string(ipc.workerMsgKey()),
            std::to_string(ipc.cashierMsgKey()),
            std::to_string(ipc.entryGateMsgKey())
        });
    }

    TestResult runSimulationLoop(const TestScenario& scenario, IpcManager& ipc, pid_t lowerWorkerPid) {
        TestResult result;
        result.testName = scenario.name;

        time_t startTime = time(nullptr);
        bool emergencyTriggered = false;
        bool resumeTriggered = false;
        uint32_t maxObservedCapacity = 0;
        SharedRopewayState lastState{};

        std::cout << "[Test] Simulation running..." << std::endl;

        while (!SignalHelper::shouldExit(signals_)) {
            time_t elapsed = time(nullptr) - startTime;

            {
                // Lock all for consistent snapshot (needed for deadlock check)
                Semaphore::ScopedLock lockCore(ipc.sem(), Semaphore::Index::SHM_OPERATIONAL);
                Semaphore::ScopedLock lockChairs(ipc.sem(), Semaphore::Index::SHM_CHAIRS);
                Semaphore::ScopedLock lockStats(ipc.sem(), Semaphore::Index::SHM_STATS);

                uint32_t currentCapacity = ipc.state()->operational.touristsInLowerStation;
                if (currentCapacity > maxObservedCapacity) {
                    maxObservedCapacity = currentCapacity;
                }

                if (ipc.state()->operational.state == RopewayState::STOPPED) {
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
                if (lowerWorkerPid > 0) {
                    kill(lowerWorkerPid, SIGUSR1);
                }
                emergencyTriggered = true;
            }

            if (scenario.resumeAtSec > 0 &&
                elapsed >= scenario.resumeAtSec && emergencyTriggered && !resumeTriggered) {
                std::cout << "[Test] >>> TRIGGERING RESUME <<<\n";
                if (lowerWorkerPid > 0) {
                    kill(lowerWorkerPid, SIGUSR2);
                }
                resumeTriggered = true;
            }

            if (elapsed >= scenario.simulationDurationSec) {
                std::cout << "[Test] Simulation timeout reached.\n";
                break;
            }

            usleep(Config::Time::MAIN_LOOP_POLL_US);
        }

        {
            // Lock all for final validation snapshot
            Semaphore::ScopedLock lockCore(ipc.sem(), Semaphore::Index::SHM_OPERATIONAL);
            Semaphore::ScopedLock lockChairs(ipc.sem(), Semaphore::Index::SHM_CHAIRS);
            Semaphore::ScopedLock lockStats(ipc.sem(), Semaphore::Index::SHM_STATS);
            ipc.state()->stats.dailyStats.simulationEndTime = time(nullptr);
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