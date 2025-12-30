#pragma once

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "tests/TestConfig.hpp"
#include "tests/TestValidator.hpp"
#include "ipc/SharedMemory.hpp"
#include "ipc/Semaphore.hpp"
#include "ipc/MessageQueue.hpp"
#include "ipc/ropeway_system_state.hpp"
#include "ipc/worker_message.hpp"
#include "ipc/cashier_message.hpp"
#include "ipc/semaphore_index.hpp"
#include "common/config.hpp"

namespace Test {

/**
 * Test runner that executes test scenarios and collects results
 */
class TestRunner {
public:
    TestRunner() : shmKey_{Config::Ipc::SHM_KEY_BASE + 100},  // Offset for tests
                   semKey_{Config::Ipc::SEM_KEY_BASE + 100},
                   msgKey_{Config::Ipc::MSG_KEY_BASE + 100},
                   cashierMsgKey_{Config::Ipc::MSG_KEY_BASE + 101} {
        setupSignalHandlers();
    }

    /**
     * Run all predefined test scenarios
     */
    std::vector<TestResult> runAllTests() {
        std::vector<TestResult> results;

        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "           ROPEWAY SIMULATION - AUTOMATED TEST SUITE\n";
        std::cout << std::string(70, '=') << "\n\n";

        // Run each test
        results.push_back(runTest(Scenarios::createCapacityLimitTest()));
        results.push_back(runTest(Scenarios::createChildSupervisionTest()));
        results.push_back(runTest(Scenarios::createVipPriorityTest()));
        results.push_back(runTest(Scenarios::createEmergencyStopTest()));

        // Print summary
        printSummary(results);

        return results;
    }

    /**
     * Run a specific test scenario
     */
    TestResult runTest(const TestScenario& scenario) {
        std::cout << "\n" << std::string(60, '-') << "\n";
        std::cout << "Running: " << scenario.name << "\n";
        std::cout << "Description: " << scenario.description << "\n";
        std::cout << std::string(60, '-') << "\n";

        TestResult result;
        result.testName = scenario.name;

        try {
            // Cleanup any previous IPC structures
            cleanupIpc();

            // Create IPC structures
            SharedMemory<RopewaySystemState> shm(shmKey_, true);
            Semaphore sem(semKey_, SemaphoreIndex::TOTAL_SEMAPHORES, true);
            MessageQueue<WorkerMessage> workerMsgQueue(msgKey_, true);
            MessageQueue<TicketRequest> cashierMsgQueue(cashierMsgKey_, true);

            // Initialize semaphores
            sem.setValue(SemaphoreIndex::STATION_CAPACITY, scenario.stationCapacity);
            sem.setValue(SemaphoreIndex::SHARED_MEMORY, 1);
            sem.setValue(SemaphoreIndex::ENTRY_GATES, Config::Gate::NUM_ENTRY_GATES);
            sem.setValue(SemaphoreIndex::RIDE_GATES, Config::Gate::NUM_RIDE_GATES);
            sem.setValue(SemaphoreIndex::CHAIR_ALLOCATION, 1);
            sem.setValue(SemaphoreIndex::WORKER_SYNC, 0);

            // Initialize shared state
            time_t startTime = time(nullptr);
            shm->state = RopewayState::RUNNING;
            shm->acceptingNewTourists = true;
            shm->openingTime = startTime;
            shm->closingTime = startTime + scenario.simulationDurationSec + 10;
            shm->dailyStats.simulationStartTime = startTime;

            std::cout << "[Test] Station capacity N = " << scenario.stationCapacity << "\n";
            std::cout << "[Test] Simulation duration: " << scenario.simulationDurationSec << "s\n";
            std::cout << "[Test] Tourists: " << scenario.tourists.size() << "\n";

            // Spawn processes
            pid_t cashierPid = spawnCashier();
            usleep(100000);

            pid_t worker1Pid = spawnWorker("worker1_process");
            pid_t worker2Pid = spawnWorker("worker2_process");
            shm->worker1Pid = worker1Pid;
            shm->worker2Pid = worker2Pid;
            usleep(200000);

            std::cout << "[Test] Spawning " << scenario.tourists.size() << " tourists...\n";

            std::vector<pid_t> touristPids;
            for (const auto& t : scenario.tourists) {
                usleep(t.spawnDelayMs * 1000);
                pid_t pid = spawnTourist(t);
                if (pid > 0) {
                    touristPids.push_back(pid);
                }
            }

            // Run simulation with monitoring
            result = runSimulationLoop(scenario, shm, sem, worker1Pid);

            // Terminate processes
            terminateProcess(cashierPid, "Cashier");
            terminateProcess(worker1Pid, "Worker1");
            terminateProcess(worker2Pid, "Worker2");

            for (pid_t pid : touristPids) {
                kill(pid, SIGTERM);
            }

            usleep(300000);

            // Check for zombies
            result.zombieProcesses = TestValidator::checkForZombies();
            if (result.zombieProcesses > 0 && scenario.expectNoZombies) {
                std::ostringstream oss;
                oss << "ZOMBIES DETECTED: " << result.zombieProcesses << " zombie process(es)";
                result.addFailure(oss.str());
            }

            // Cleanup remaining processes
            while (waitpid(-1, nullptr, WNOHANG) > 0) {}

            // Record simulation duration
            result.simulationDuration = time(nullptr) - startTime;

        } catch (const std::exception& e) {
            result.addFailure(std::string("EXCEPTION: ") + e.what());
        }

        // Cleanup IPC
        cleanupIpc();

        // Print result
        printTestResult(result);

        return result;
    }

private:
    key_t shmKey_;
    key_t semKey_;
    key_t msgKey_;
    key_t cashierMsgKey_;

    static volatile sig_atomic_t shouldExit_;

    static void signalHandler(int) {
        shouldExit_ = 1;
    }

    void setupSignalHandlers() {
        struct sigaction sa{};
        sa.sa_handler = signalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
        signal(SIGCHLD, SIG_IGN);
    }

    void cleanupIpc() {
        SharedMemory<RopewaySystemState>::removeByKey(shmKey_);
        Semaphore::removeByKey(semKey_);
        MessageQueue<WorkerMessage>::removeByKey(msgKey_);
        MessageQueue<TicketRequest>::removeByKey(cashierMsgKey_);
    }

    std::string getProcessPath(const char* processName) {
        char path[1024];
        uint32_t size = sizeof(path);

        #ifdef __APPLE__
        if (_NSGetExecutablePath(path, &size) != 0) {
            return std::string("./") + processName;
        }
        #else
        ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (len == -1) {
            return std::string("./") + processName;
        }
        path[len] = '\0';
        #endif

        char* lastSlash = strrchr(path, '/');
        if (lastSlash != nullptr) {
            strcpy(lastSlash + 1, processName);
            return path;
        }
        return std::string("./") + processName;
    }

    pid_t spawnWorker(const char* processName) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            return -1;
        }

        if (pid == 0) {
            std::string processPath = getProcessPath(processName);
            char shmStr[16], semStr[16], msgStr[16];
            snprintf(shmStr, sizeof(shmStr), "%d", shmKey_);
            snprintf(semStr, sizeof(semStr), "%d", semKey_);
            snprintf(msgStr, sizeof(msgStr), "%d", msgKey_);
            execl(processPath.c_str(), processName, shmStr, semStr, msgStr, nullptr);
            perror("execl");
            _exit(1);
        }

        return pid;
    }

    pid_t spawnCashier() {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            return -1;
        }

        if (pid == 0) {
            std::string processPath = getProcessPath("cashier_process");
            char shmStr[16], semStr[16], cashierMsgStr[16];
            snprintf(shmStr, sizeof(shmStr), "%d", shmKey_);
            snprintf(semStr, sizeof(semStr), "%d", semKey_);
            snprintf(cashierMsgStr, sizeof(cashierMsgStr), "%d", cashierMsgKey_);
            execl(processPath.c_str(), "cashier_process", shmStr, semStr, cashierMsgStr, nullptr);
            perror("execl");
            _exit(1);
        }

        return pid;
    }

    pid_t spawnTourist(const TouristTestConfig& t) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            return -1;
        }

        if (pid == 0) {
            std::string processPath = getProcessPath("tourist_process");
            char idStr[16], ageStr[16], typeStr[16], vipStr[16], rideStr[16];
            char guardianStr[16], trailStr[16], shmStr[16], semStr[16], msgStr[16], cashierMsgStr[16];

            snprintf(idStr, sizeof(idStr), "%u", t.id);
            snprintf(ageStr, sizeof(ageStr), "%u", t.age);
            snprintf(typeStr, sizeof(typeStr), "%d", static_cast<int>(t.type));
            snprintf(vipStr, sizeof(vipStr), "%d", t.requestVip ? 1 : 0);
            snprintf(rideStr, sizeof(rideStr), "%d", t.wantsToRide ? 1 : 0);
            snprintf(guardianStr, sizeof(guardianStr), "%d", t.guardianId);
            snprintf(trailStr, sizeof(trailStr), "%d", static_cast<int>(t.trail));
            snprintf(shmStr, sizeof(shmStr), "%d", shmKey_);
            snprintf(semStr, sizeof(semStr), "%d", semKey_);
            snprintf(msgStr, sizeof(msgStr), "%d", msgKey_);
            snprintf(cashierMsgStr, sizeof(cashierMsgStr), "%d", cashierMsgKey_);

            execl(processPath.c_str(), "tourist_process",
                  idStr, ageStr, typeStr, vipStr, rideStr,
                  guardianStr, trailStr, shmStr, semStr, msgStr, cashierMsgStr,
                  nullptr);
            perror("execl");
            _exit(1);
        }

        return pid;
    }

    void terminateProcess(pid_t pid, const char* name) {
        if (pid > 0) {
            kill(pid, SIGTERM);
            usleep(100000);
            kill(pid, SIGKILL);
        }
    }

    TestResult runSimulationLoop(const TestScenario& scenario,
                                  SharedMemory<RopewaySystemState>& shm,
                                  Semaphore& sem,
                                  pid_t worker1Pid) {
        TestResult result;
        result.testName = scenario.name;

        time_t startTime = time(nullptr);
        bool emergencyTriggered = false;
        bool resumeTriggered = false;
        uint32_t maxObservedCapacity = 0;
        RopewaySystemState lastState{};

        std::cout << "[Test] Simulation running..." << std::endl;

        while (!shouldExit_) {
            time_t elapsed = time(nullptr) - startTime;

            // Monitor capacity
            {
                SemaphoreLock lock(sem, SemaphoreIndex::SHARED_MEMORY);
                uint32_t currentCapacity = shm->touristsInLowerStation;
                if (currentCapacity > maxObservedCapacity) {
                    maxObservedCapacity = currentCapacity;
                }

                // Check for state changes
                if (shm->state == RopewayState::STOPPED) {
                    std::cout << "[Test] Ropeway stopped.\n";
                    break;
                }

                // Deadlock detection (every 5 seconds)
                if (elapsed > 0 && elapsed % 5 == 0) {
                    if (TestValidator::checkForDeadlock(lastState, *shm, 5)) {
                        result.addWarning("Possible deadlock detected - no progress for 5 seconds");
                    }
                    lastState = *shm;
                }
            }

            // Trigger emergency stop
            if (scenario.emergencyStopAtSec > 0 &&
                elapsed >= scenario.emergencyStopAtSec && !emergencyTriggered) {
                std::cout << "[Test] >>> TRIGGERING EMERGENCY STOP <<<\n";
                if (worker1Pid > 0) {
                    kill(worker1Pid, SIGUSR1);
                }
                emergencyTriggered = true;
            }

            // Trigger resume
            if (scenario.resumeAtSec > 0 &&
                elapsed >= scenario.resumeAtSec && emergencyTriggered && !resumeTriggered) {
                std::cout << "[Test] >>> TRIGGERING RESUME <<<\n";
                if (worker1Pid > 0) {
                    kill(worker1Pid, SIGUSR2);
                }
                resumeTriggered = true;
            }

            // Timeout
            if (elapsed >= scenario.simulationDurationSec) {
                std::cout << "[Test] Simulation timeout reached.\n";
                break;
            }

            usleep(200000);  // 200ms polling
        }

        // Final validation
        {
            SemaphoreLock lock(sem, SemaphoreIndex::SHARED_MEMORY);
            shm->dailyStats.simulationEndTime = time(nullptr);
            result = TestValidator::validate(scenario, *shm, maxObservedCapacity);
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

        // Print failures
        for (const auto& failure : result.failures) {
            std::cout << "  [FAIL] " << failure << "\n";
        }

        // Print warnings/info (first 5)
        uint32_t warnCount = 0;
        for (const auto& warning : result.warnings) {
            std::cout << "  [INFO] " << warning << "\n";
            if (++warnCount >= 5) {
                std::cout << "  ... (" << (result.warnings.size() - 5) << " more)\n";
                break;
            }
        }

        // Print metrics
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

// Static member definition
volatile sig_atomic_t TestRunner::shouldExit_ = 0;

} // namespace Test
