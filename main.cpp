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
#include "utils/Logger.hpp"
#include "structures/tourist.hpp"

namespace {
    SignalHelper::SignalFlags g_signals;
    pid_t g_worker1Pid = 0;
    pid_t g_worker2Pid = 0;
    pid_t g_cashierPid = 0;
    constexpr const char* TAG = "Main";

    pid_t spawnTourist(uint32_t id, uint32_t age, TouristType type, bool isVip,
                       bool wantsToRide, int32_t guardianId, TrailDifficulty trail,
                       key_t shmKey, key_t semKey, key_t msgKey, key_t cashierMsgKey) {
        return ProcessSpawner::spawn("tourist_process", {
            std::to_string(id),
            std::to_string(age),
            std::to_string(static_cast<int>(type)),
            std::to_string(isVip ? 1 : 0),
            std::to_string(wantsToRide ? 1 : 0),
            std::to_string(guardianId),
            std::to_string(static_cast<int>(trail)),
            std::to_string(shmKey),
            std::to_string(semKey),
            std::to_string(msgKey),
            std::to_string(cashierMsgKey)
        });
    }
}

int main() {
    Logger::separator('=', 60);
    Logger::log("           ROPEWAY SIMULATION");
    Logger::separator('=', 60);

    SignalHelper::setup(g_signals, SignalHelper::Mode::ORCHESTRATOR);
    IpcManager::cleanup(Config::Ipc::SHM_KEY_BASE);

    try {
        Logger::info(TAG, "Creating IPC structures...");

        IpcManager ipc(Config::Ipc::SHM_KEY_BASE, true);

        constexpr int TEST_STATION_CAPACITY = 10;
        Logger::info(TAG, "Station capacity set to ", TEST_STATION_CAPACITY);

        ipc.initializeSemaphores(TEST_STATION_CAPACITY);

        time_t simulationStartTime = time(nullptr);
        ipc.initializeState(simulationStartTime, simulationStartTime + 25);

        Logger::info(TAG, "IPC structures initialized");

        Logger::info(TAG, "Spawning cashier...");
        g_cashierPid = ProcessSpawner::spawnWithKeys("cashier_process",
            ipc.shmKey(), ipc.semKey(), ipc.cashierMsgKey());
        if (g_cashierPid > 0) {
            Logger::info(TAG, "Cashier spawned with PID ", g_cashierPid);
        }
        // Wait for cashier to signal ready (proper synchronization via semaphore)
        ipc.semaphores().wait(SemaphoreIndex::CASHIER_READY);
        Logger::info(TAG, "Cashier ready");

        Logger::info(TAG, "Spawning workers...");

        g_worker1Pid = ProcessSpawner::spawnWithKeys("worker1_process",
            ipc.shmKey(), ipc.semKey(), ipc.msgKey());
        if (g_worker1Pid > 0) {
            Logger::info(TAG, "Worker1 spawned with PID ", g_worker1Pid);
        }

        g_worker2Pid = ProcessSpawner::spawnWithKeys("worker2_process",
            ipc.shmKey(), ipc.semKey(), ipc.msgKey());
        if (g_worker2Pid > 0) {
            Logger::info(TAG, "Worker2 spawned with PID ", g_worker2Pid);
        }

        // Wait for workers to signal ready (proper synchronization via semaphores)
        ipc.semaphores().wait(SemaphoreIndex::WORKER1_READY);
        Logger::info(TAG, "Worker1 ready");
        ipc.semaphores().wait(SemaphoreIndex::WORKER2_READY);
        Logger::info(TAG, "Worker2 ready");

        std::vector<pid_t> touristPids;
        Logger::info(TAG, "Spawning tourists...");

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
            Logger::info(TAG, "Spawning Tourist ", t.id, ": ", t.description);
            pid_t pid = spawnTourist(t.id, t.age, t.type, t.isVip, t.wantsToRide, t.guardianId, t.trail,
                                     ipc.shmKey(), ipc.semKey(), ipc.msgKey(), ipc.cashierMsgKey());
            if (pid > 0) {
                touristPids.push_back(pid);
            }
            usleep(Config::Timing::TOURIST_LOOP_POLL_US);  // Staggered tourist spawning
        }

        Logger::info(TAG, "Simulation running...");
        Logger::info(TAG, "Emergency stop scheduled at 8 seconds");
        Logger::info(TAG, "Resume scheduled at 13 seconds");

        time_t startTime = time(nullptr);
        bool emergencyTriggered = false;
        bool resumeTriggered = false;

        while (!SignalHelper::shouldExit(g_signals)) {
            time_t elapsed = time(nullptr) - startTime;

            RopewayState currentState;
            {
                SemaphoreLock lock(ipc.semaphores(), SemaphoreIndex::SHARED_MEMORY);
                currentState = ipc.state()->core.state;
            }

            if (currentState == RopewayState::STOPPED) {
                Logger::info(TAG, "Ropeway stopped. Ending simulation.");
                break;
            }

            if (elapsed >= 8 && !emergencyTriggered) {
                Logger::info(TAG, ">>> TRIGGERING EMERGENCY STOP <<<");
                Logger::info(TAG, "Sending SIGUSR1 to Worker1 (PID: ", g_worker1Pid, ")");
                if (g_worker1Pid > 0) {
                    kill(g_worker1Pid, SIGUSR1);
                }
                emergencyTriggered = true;
            }

            if (elapsed >= 13 && emergencyTriggered && !resumeTriggered) {
                Logger::info(TAG, ">>> TRIGGERING RESUME <<<");
                Logger::info(TAG, "Sending SIGUSR2 to Worker1 (PID: ", g_worker1Pid, ")");
                if (g_worker1Pid > 0) {
                    kill(g_worker1Pid, SIGUSR2);
                }
                resumeTriggered = true;
            }

            if (elapsed >= 30) {
                Logger::info(TAG, "Timeout reached.");
                break;
            }

            usleep(Config::Timing::MAIN_LOOP_POLL_US);
        }

        time_t simulationEndTime = time(nullptr);

        std::stringstream report;
        {
            SemaphoreLock lock(ipc.semaphores(), SemaphoreIndex::SHARED_MEMORY);
            ipc.state()->stats.dailyStats.simulationEndTime = simulationEndTime;

            char startTimeStr[64], endTimeStr[64];
            struct tm* tm_start = localtime(&ipc.state()->stats.dailyStats.simulationStartTime);
            strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%d %H:%M:%S", tm_start);
            struct tm* tm_end = localtime(&ipc.state()->stats.dailyStats.simulationEndTime);
            strftime(endTimeStr, sizeof(endTimeStr), "%Y-%m-%d %H:%M:%S", tm_end);

            report << std::string(60, '=') << "\n";
            report << "           DAILY REPORT - ROPEWAY SIMULATION\n";
            report << std::string(60, '=') << "\n\n";

            report << "--- SIMULATION TIMING ---\n";
            report << "Start Time: " << startTimeStr << "\n";
            report << "End Time:   " << endTimeStr << "\n";
            report << "Duration:   " << (simulationEndTime - ipc.state()->stats.dailyStats.simulationStartTime) << " seconds\n";

            report << "\n--- OVERALL STATISTICS ---\n";
            report << "Final State: " << EnumStrings::toString(ipc.state()->core.state) << "\n";

            const DailyStatistics& dailyStats = ipc.state()->stats.dailyStats;
            report << "Total Tourists Served: " << dailyStats.totalTourists << "\n";
            report << "  - VIP Tourists: " << dailyStats.vipTourists << "\n";
            report << "  - Children (under 10): " << dailyStats.childrenServed << "\n";
            report << "  - Seniors (65+): " << dailyStats.seniorsServed << "\n";
            report << "Total Rides Completed: " << dailyStats.totalRides << "\n";
            report << "  - Cyclist Rides: " << dailyStats.cyclistRides << "\n";
            report << "  - Pedestrian Rides: " << dailyStats.pedestrianRides << "\n";
            report << "Total Revenue: " << std::fixed << std::setprecision(2) << dailyStats.totalRevenueWithDiscounts << "\n";
            report << "Emergency Stops: " << dailyStats.emergencyStops << "\n";

            report << "\n--- RIDES PER TOURIST/TICKET ---\n";
            report << std::left << std::setw(10) << "Tourist"
                   << std::setw(10) << "Ticket"
                   << std::setw(8) << "Age"
                   << std::setw(12) << "Type"
                   << std::setw(6) << "VIP"
                   << std::setw(8) << "Rides\n";
            report << std::string(54, '-') << "\n";

            for (uint32_t i = 0; i < ipc.state()->stats.touristRecordCount; ++i) {
                const TouristRideRecord& rec = ipc.state()->stats.touristRecords[i];
                report << std::left << std::setw(10) << rec.touristId
                       << std::setw(10) << rec.ticketId
                       << std::setw(8) << rec.age
                       << std::setw(12) << EnumStrings::toString(rec.type)
                       << std::setw(6) << (rec.isVip ? "Yes" : "No")
                       << std::setw(8) << rec.ridesCompleted << "\n";
            }

            report << std::string(60, '=') << "\n";
        }

        // Write report to stdout using write()
        std::string reportStr = report.str();
        write(STDOUT_FILENO, "\n", 1);
        write(STDOUT_FILENO, reportStr.c_str(), reportStr.length());

        std::string reportFilename = "daily_report_" + std::to_string(simulationEndTime) + ".txt";
        std::ofstream reportFile(reportFilename);
        if (reportFile.is_open()) {
            reportFile << reportStr;
            reportFile.close();
            Logger::info(TAG, "Daily report saved to: ", reportFilename.c_str(), "");
        }

        Logger::info(TAG, "Cleaning up processes...");

        ProcessSpawner::terminate(g_cashierPid, "Cashier");
        ProcessSpawner::terminate(g_worker1Pid, "Worker1");
        ProcessSpawner::terminate(g_worker2Pid, "Worker2");
        ProcessSpawner::terminateAll(touristPids);

        usleep(Config::Timing::PROCESS_CLEANUP_WAIT_US);
        ProcessSpawner::waitForAll();

    } catch (const std::exception& e) {
        Logger::perr(TAG, e.what());
        ProcessSpawner::terminate(g_cashierPid, "Cashier");
        ProcessSpawner::terminate(g_worker1Pid, "Worker1");
        ProcessSpawner::terminate(g_worker2Pid, "Worker2");
        IpcManager::cleanup(Config::Ipc::SHM_KEY_BASE);
        return 1;
    }

    Logger::info(TAG, "Cleaning up IPC structures...");
    IpcManager::cleanup(Config::Ipc::SHM_KEY_BASE);
    Logger::info(TAG, "Done.");

    return 0;
}
