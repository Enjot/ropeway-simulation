#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>

#include "tests/TestRunner.hpp"
#include "tests/TestConfig.hpp"
#include "core/Config.hpp"

namespace {
    void printUsage(const char *programName) {
        std::cout << "Usage: " << programName << " [OPTIONS]\n\n";
        std::cout << "Options:\n";
        std::cout << "  --all           Run all tests (default)\n";
        std::cout << "  --test <n>      Run specific test (1-4)\n";
        std::cout << "  --list          List available tests\n";
        std::cout << "  --output <file> Save results to file\n";
        std::cout << "  --help          Show this help\n\n";
        std::cout << "Tests:\n";
        std::cout << "  1 - Station Capacity Limit (N=5, 30 tourists, 60s)\n";
        std::cout << "  2 - Child Supervision (6 children <8, 3 adults, 20 tourists, 90s)\n";
        std::cout << "  3 - VIP Priority (10 VIP = 10%, 100 tourists, 120s)\n";
        std::cout << "  4 - Emergency Stop/Resume (20 tourists, trigger at 20s, resume at 30s, 60s)\n";
        std::cout << "  5 - STRESS: High Load (1000 tourists, 10 VIP, 180s)\n";
        std::cout << "  6 - STRESS: Queue Saturation (200 tourists burst, N=10, 60s)\n";
    }

    void listTests() {
        std::cout << "\n=== Available Test Scenarios ===\n\n";

        auto test1 = Test::Scenarios::createCapacityLimitTest();
        std::cout << "Test 1: " << test1.name << "\n";
        std::cout << "        " << test1.description << "\n";
        std::cout << "        Params: N=" << test1.stationCapacity
                << ", tourists=" << test1.tourists.size()
                << ", duration=" << test1.simulationDurationSec << "s\n\n";

        auto test2 = Test::Scenarios::createChildSupervisionTest();
        std::cout << "Test 2: " << test2.name << "\n";
        std::cout << "        " << test2.description << "\n";
        std::cout << "        Params: N=" << test2.stationCapacity
                << ", tourists=" << test2.tourists.size()
                << ", duration=" << test2.simulationDurationSec << "s\n\n";

        auto test3 = Test::Scenarios::createVipPriorityTest();
        std::cout << "Test 3: " << test3.name << "\n";
        std::cout << "        " << test3.description << "\n";
        std::cout << "        Params: N=" << test3.stationCapacity
                << ", tourists=" << test3.tourists.size()
                << ", duration=" << test3.simulationDurationSec << "s\n\n";

        auto test4 = Test::Scenarios::createEmergencyStopTest();
        std::cout << "Test 4: " << test4.name << "\n";
        std::cout << "        " << test4.description << "\n";
        std::cout << "        Params: N=" << test4.stationCapacity
                << ", tourists=" << test4.tourists.size()
                << ", duration=" << test4.simulationDurationSec << "s\n";
        std::cout << "        Emergency at " << test4.emergencyStopAtSec << "s, resume at "
                << test4.resumeAtSec << "s\n\n";

        auto test5 = Test::Scenarios::createStressTest();
        std::cout << "Test 5: " << test5.name << " [STRESS]\n";
        std::cout << "        " << test5.description << "\n";
        std::cout << "        Params: N=" << test5.stationCapacity
                << ", tourists=" << test5.tourists.size()
                << ", duration=" << test5.simulationDurationSec << "s\n\n";

        auto test6 = Test::Scenarios::createQueueSaturationTest();
        std::cout << "Test 6: " << test6.name << " [STRESS]\n";
        std::cout << "        " << test6.description << "\n";
        std::cout << "        Params: N=" << test6.stationCapacity
                << ", tourists=" << test6.tourists.size()
                << ", duration=" << test6.simulationDurationSec << "s\n\n";
    }

    void saveResultsToFile(const std::vector<Test::TestResult> &results, const std::string &filename) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open file " << filename << " for writing\n";
            return;
        }

        file << "=== ROPEWAY SIMULATION TEST RESULTS ===\n\n";

        time_t now = time(nullptr);
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&now));
        file << "Generated: " << timeStr << "\n\n";

        uint32_t passed = 0;
        uint32_t failed = 0;

        for (const auto &r: results) {
            file << "--- " << r.testName << " ---\n";
            file << "Status: " << (r.passed ? "PASSED" : "FAILED") << "\n";

            if (!r.failures.empty()) {
                file << "Failures:\n";
                for (const auto &f: r.failures) {
                    file << "  - " << f << "\n";
                }
            }

            if (!r.warnings.empty()) {
                file << "Info:\n";
                for (const auto &w: r.warnings) {
                    file << "  - " << w << "\n";
                }
            }

            file << "Metrics:\n";
            file << "  - Max capacity observed: " << r.maxObservedCapacity << "\n";
            file << "  - Total rides completed: " << r.totalRidesCompleted << "\n";
            file << "  - Emergency stops: " << r.emergencyStopsTriggered << "\n";
            file << "  - Emergencies resumed: " << r.emergenciesResumed << "\n";
            file << "  - Zombie processes: " << r.zombieProcesses << "\n";
            file << "  - Simulation duration: " << r.simulationDuration << "s\n";
            file << "\n";

            if (r.passed) ++passed;
            else ++failed;
        }

        file << "=== SUMMARY ===\n";
        file << "Total: " << results.size() << " tests\n";
        file << "Passed: " << passed << "\n";
        file << "Failed: " << failed << "\n";

        file.close();
        std::cout << "Results saved to: " << filename << "\n";
    }
} // anonymous namespace

int main(int argc, char *argv[]) {
    bool runAll = true;
    int specificTest = 0;
    std::string outputFile;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--list") == 0) {
            listTests();
            return 0;
        } else if (strcmp(argv[i], "--all") == 0) {
            runAll = true;
        } else if (strcmp(argv[i], "--test") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: --test requires a test number (1-4)\n";
                return 1;
            }
            specificTest = atoi(argv[++i]);
            if (specificTest < 1 || specificTest > 6) {
                std::cerr << "Error: Test number must be 1-6\n";
                return 1;
            }
            runAll = false;
        } else if (strcmp(argv[i], "--output") == 0 || strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: --output requires a filename\n";
                return 1;
            }
            outputFile = argv[++i];
        } else {
            std::cerr << "Error: Unknown option: " << argv[i] << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    try {
        Config::validate();
    } catch (const std::exception& e) {
        std::cerr << "Config error: " << e.what() << "\n";
        std::cerr << "Run: source ropeway.env && ./test_runner\n";
        return 1;
    }

    Test::TestRunner runner;
    std::vector<Test::TestResult> results;

    if (runAll) {
        results = runner.runAllTests();
    } else {
        Test::TestScenario scenario;
        switch (specificTest) {
            case 1:
                scenario = Test::Scenarios::createCapacityLimitTest();
                break;
            case 2:
                scenario = Test::Scenarios::createChildSupervisionTest();
                break;
            case 3:
                scenario = Test::Scenarios::createVipPriorityTest();
                break;
            case 4:
                scenario = Test::Scenarios::createEmergencyStopTest();
                break;
            case 5:
                scenario = Test::Scenarios::createStressTest();
                break;
            case 6:
                scenario = Test::Scenarios::createQueueSaturationTest();
                break;
            default:
                std::cerr << "Invalid test number\n";
                return 1;
        }
        results.push_back(runner.runTest(scenario));
    }

    // Save to file if requested
    if (!outputFile.empty()) {
        saveResultsToFile(results, outputFile);
    }

    // Return exit code based on test results
    for (const auto &r: results) {
        if (!r.passed) {
            return 1;
        }
    }

    return 0;
}
