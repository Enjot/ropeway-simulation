#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>

#include "ipc/IpcManager.hpp"
#include "utils/SignalHelper.hpp"
#include "utils/ProcessSpawner.hpp"
#include "utils/EnumStrings.hpp"
#include "structures/tourist.hpp"

namespace {
    SignalHelper::SignalFlags g_signals;
    pid_t g_worker1Pid = 0;
    pid_t g_worker2Pid = 0;
    pid_t g_cashierPid = 0;

    pid_t spawnTourist(uint32_t id, uint32_t age, TouristType type, bool isVip,
                       bool wantsToRide, int32_t guardianId, TrailDifficulty trail,
                       key_t shmKey, key_t semKey, key_t msgKey, key_t cashierMsgKey) {
        char idStr[16], ageStr[16], typeStr[16], vipStr[16], rideStr[16];
        char guardianStr[16], trailStr[16], shmStr[16], semStr[16], msgStr[16], cashierMsgStr[16];

        snprintf(idStr, sizeof(idStr), "%u", id);
        snprintf(ageStr, sizeof(ageStr), "%u", age);
        snprintf(typeStr, sizeof(typeStr), "%d", static_cast<int>(type));
        snprintf(vipStr, sizeof(vipStr), "%d", isVip ? 1 : 0);
        snprintf(rideStr, sizeof(rideStr), "%d", wantsToRide ? 1 : 0);
        snprintf(guardianStr, sizeof(guardianStr), "%d", guardianId);
        snprintf(trailStr, sizeof(trailStr), "%d", static_cast<int>(trail));
        snprintf(shmStr, sizeof(shmStr), "%d", shmKey);
        snprintf(semStr, sizeof(semStr), "%d", semKey);
        snprintf(msgStr, sizeof(msgStr), "%d", msgKey);
        snprintf(cashierMsgStr, sizeof(cashierMsgStr), "%d", cashierMsgKey);

        return ProcessSpawner::spawn("tourist_process", {
            idStr, ageStr, typeStr, vipStr, rideStr,
            guardianStr, trailStr, shmStr, semStr, msgStr, cashierMsgStr
        });
    }
}

int main() {
    std::cout << "=== Ropeway Simulation ===" << std::endl;

    SignalHelper::setup(g_signals, SignalHelper::Mode::ORCHESTRATOR);
    IpcManager::cleanup(Config::Ipc::SHM_KEY_BASE);

    try {
        std::cout << "[Main] Creating IPC structures..." << std::endl;

        IpcManager ipc(Config::Ipc::SHM_KEY_BASE, true);

        constexpr int TEST_STATION_CAPACITY = 10;
        std::cout << "[Main] Station capacity set to " << TEST_STATION_CAPACITY << std::endl;

        ipc.initializeSemaphores(TEST_STATION_CAPACITY);

        time_t simulationStartTime = time(nullptr);
        ipc.initializeState(simulationStartTime, simulationStartTime + 25);

        std::cout << "[Main] IPC structures initialized" << std::endl;

        std::cout << "\n[Main] Spawning cashier..." << std::endl;
        g_cashierPid = ProcessSpawner::spawnWithKeys("cashier_process",
            ipc.shmKey(), ipc.semKey(), ipc.cashierMsgKey());
        if (g_cashierPid > 0) {
            std::cout << "[Main] Cashier spawned with PID " << g_cashierPid << std::endl;
        }
        usleep(100000);

        std::cout << "\n[Main] Spawning workers..." << std::endl;

        g_worker1Pid = ProcessSpawner::spawnWithKeys("worker1_process",
            ipc.shmKey(), ipc.semKey(), ipc.msgKey());
        if (g_worker1Pid > 0) {
            std::cout << "[Main] Worker1 spawned with PID " << g_worker1Pid << std::endl;
        }

        g_worker2Pid = ProcessSpawner::spawnWithKeys("worker2_process",
            ipc.shmKey(), ipc.semKey(), ipc.msgKey());
        if (g_worker2Pid > 0) {
            std::cout << "[Main] Worker2 spawned with PID " << g_worker2Pid << std::endl;
        }

        usleep(200000);

        std::vector<pid_t> touristPids;
        std::cout << "\n[Main] Spawning tourists..." << std::endl;

        struct TouristConfig {
            uint32_t id;
            uint32_t age;
            TouristType type;
            bool isVip;
            bool wantsToRide;
            int32_t guardianId;
            TrailDifficulty trail;
            const char* description;
        };

        std::vector<TouristConfig> tourists = {
            {1, 35, TouristType::PEDESTRIAN, true, true, -1, TrailDifficulty::EASY, "Adult VIP 1"},
            {2, 6, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY, "Child 6yo (needs guardian)"},
            {3, 70, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY, "Senior 70yo (25% discount)"},
            {4, 40, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY, "Adult 2 (potential guardian)"},
            {5, 7, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY, "Child 7yo (needs guardian)"},
            {6, 30, TouristType::CYCLIST, false, true, -1, TrailDifficulty::MEDIUM, "Adult cyclist"},
            {7, 9, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY, "Child 9yo (discount, no guardian)"},
            {8, 25, TouristType::PEDESTRIAN, true, true, -1, TrailDifficulty::EASY, "Adult VIP 2"},
        };

        for (const auto& t : tourists) {
            std::cout << "[Main] Spawning Tourist " << t.id << ": " << t.description << std::endl;
            pid_t pid = spawnTourist(t.id, t.age, t.type, t.isVip, t.wantsToRide, t.guardianId, t.trail,
                                     ipc.shmKey(), ipc.semKey(), ipc.msgKey(), ipc.cashierMsgKey());
            if (pid > 0) {
                touristPids.push_back(pid);
            }
            usleep(100000);
        }

        std::cout << "\n[Main] Simulation running..." << std::endl;
        std::cout << "[Main] Emergency stop scheduled at 8 seconds" << std::endl;
        std::cout << "[Main] Resume scheduled at 13 seconds" << std::endl;

        time_t startTime = time(nullptr);
        bool emergencyTriggered = false;
        bool resumeTriggered = false;

        while (!SignalHelper::shouldExit(g_signals)) {
            time_t elapsed = time(nullptr) - startTime;

            RopewayState currentState;
            {
                SemaphoreLock lock(ipc.semaphores(), SemaphoreIndex::SHARED_MEMORY);
                currentState = ipc.state()->state;
            }

            if (currentState == RopewayState::STOPPED) {
                std::cout << "\n[Main] Ropeway stopped. Ending simulation." << std::endl;
                break;
            }

            if (elapsed >= 8 && !emergencyTriggered) {
                std::cout << "\n[Main] >>> TRIGGERING EMERGENCY STOP <<<" << std::endl;
                std::cout << "[Main] Sending SIGUSR1 to Worker1 (PID: " << g_worker1Pid << ")" << std::endl;
                if (g_worker1Pid > 0) {
                    kill(g_worker1Pid, SIGUSR1);
                }
                emergencyTriggered = true;
            }

            if (elapsed >= 13 && emergencyTriggered && !resumeTriggered) {
                std::cout << "\n[Main] >>> TRIGGERING RESUME <<<" << std::endl;
                std::cout << "[Main] Sending SIGUSR2 to Worker1 (PID: " << g_worker1Pid << ")" << std::endl;
                if (g_worker1Pid > 0) {
                    kill(g_worker1Pid, SIGUSR2);
                }
                resumeTriggered = true;
            }

            if (elapsed >= 30) {
                std::cout << "\n[Main] Timeout reached." << std::endl;
                break;
            }

            usleep(500000);
        }

        time_t simulationEndTime = time(nullptr);

        std::stringstream report;
        {
            SemaphoreLock lock(ipc.semaphores(), SemaphoreIndex::SHARED_MEMORY);
            ipc.state()->dailyStats.simulationEndTime = simulationEndTime;

            char startTimeStr[64], endTimeStr[64];
            struct tm* tm_start = localtime(&ipc.state()->dailyStats.simulationStartTime);
            strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%d %H:%M:%S", tm_start);
            struct tm* tm_end = localtime(&ipc.state()->dailyStats.simulationEndTime);
            strftime(endTimeStr, sizeof(endTimeStr), "%Y-%m-%d %H:%M:%S", tm_end);

            report << std::string(60, '=') << "\n";
            report << "           DAILY REPORT - ROPEWAY SIMULATION\n";
            report << std::string(60, '=') << "\n\n";

            report << "--- SIMULATION TIMING ---\n";
            report << "Start Time: " << startTimeStr << "\n";
            report << "End Time:   " << endTimeStr << "\n";
            report << "Duration:   " << (simulationEndTime - ipc.state()->dailyStats.simulationStartTime) << " seconds\n";

            report << "\n--- OVERALL STATISTICS ---\n";
            report << "Final State: " << EnumStrings::toString(ipc.state()->state) << "\n";

            const DailyStatistics& stats = ipc.state()->dailyStats;
            report << "Total Tourists Served: " << stats.totalTourists << "\n";
            report << "  - VIP Tourists: " << stats.vipTourists << "\n";
            report << "  - Children (under 10): " << stats.childrenServed << "\n";
            report << "  - Seniors (65+): " << stats.seniorsServed << "\n";
            report << "Total Rides Completed: " << stats.totalRides << "\n";
            report << "  - Cyclist Rides: " << stats.cyclistRides << "\n";
            report << "  - Pedestrian Rides: " << stats.pedestrianRides << "\n";
            report << "Total Revenue: " << std::fixed << std::setprecision(2) << stats.totalRevenueWithDiscounts << "\n";
            report << "Emergency Stops: " << stats.emergencyStops << "\n";

            report << "\n--- RIDES PER TOURIST/TICKET ---\n";
            report << std::left << std::setw(10) << "Tourist"
                   << std::setw(10) << "Ticket"
                   << std::setw(8) << "Age"
                   << std::setw(12) << "Type"
                   << std::setw(6) << "VIP"
                   << std::setw(8) << "Rides\n";
            report << std::string(54, '-') << "\n";

            for (uint32_t i = 0; i < ipc.state()->touristRecordCount; ++i) {
                const TouristRideRecord& rec = ipc.state()->touristRecords[i];
                report << std::left << std::setw(10) << rec.touristId
                       << std::setw(10) << rec.ticketId
                       << std::setw(8) << rec.age
                       << std::setw(12) << EnumStrings::toString(rec.type)
                       << std::setw(6) << (rec.isVip ? "Yes" : "No")
                       << std::setw(8) << rec.ridesCompleted << "\n";
            }

            report << std::string(60, '=') << "\n";
        }

        std::cout << "\n" << report.str();

        std::string reportFilename = "daily_report_" + std::to_string(simulationEndTime) + ".txt";
        std::ofstream reportFile(reportFilename);
        if (reportFile.is_open()) {
            reportFile << report.str();
            reportFile.close();
            std::cout << "\n[Main] Daily report saved to: " << reportFilename << std::endl;
        }

        std::cout << "\n[Main] Cleaning up processes..." << std::endl;

        ProcessSpawner::terminate(g_cashierPid, "Cashier");
        ProcessSpawner::terminate(g_worker1Pid, "Worker1");
        ProcessSpawner::terminate(g_worker2Pid, "Worker2");
        ProcessSpawner::terminateAll(touristPids);

        usleep(300000);
        ProcessSpawner::waitForAll();

    } catch (const std::exception& e) {
        std::cerr << "[Main] Error: " << e.what() << std::endl;
        ProcessSpawner::terminate(g_cashierPid, "Cashier");
        ProcessSpawner::terminate(g_worker1Pid, "Worker1");
        ProcessSpawner::terminate(g_worker2Pid, "Worker2");
        IpcManager::cleanup(Config::Ipc::SHM_KEY_BASE);
        return 1;
    }

    std::cout << "\n[Main] Cleaning up IPC structures..." << std::endl;
    IpcManager::cleanup(Config::Ipc::SHM_KEY_BASE);
    std::cout << "[Main] Done." << std::endl;

    return 0;
}
