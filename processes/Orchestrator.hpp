#pragma once

#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <memory>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>

#include "../ipc/IpcManager.hpp"
#include "../utils/SignalHelper.hpp"
#include "../utils/ProcessSpawner.hpp"
#include "../utils/EnumStrings.hpp"
#include "../utils/Logger.hpp"
#include "../structures/tourist.hpp"

/**
 * Main orchestrator for the ropeway simulation.
 * Manages IPC resources, spawns child processes, runs simulation loop,
 * and generates reports.
 */
class Orchestrator {
public:
    static constexpr const char* TAG = "Orchestrator";
    static constexpr int TEST_STATION_CAPACITY = 10;

    Orchestrator()
        : cashierPid_{0},
          worker1Pid_{0},
          worker2Pid_{0} {
        Logger::separator('=', 60);
        Logger::log("           ROPEWAY SIMULATION");
        Logger::separator('=', 60);

        SignalHelper::setup(signals_, SignalHelper::Mode::ORCHESTRATOR);
        IpcManager::cleanup(Config::Ipc::SHM_KEY_BASE);
    }

    ~Orchestrator() {
        cleanup();
    }

    void run() {
        initializeIpc();
        spawnProcesses();
        spawnTourists();
        runSimulationLoop();
        generateReport();
    }

private:
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

    void initializeIpc() {
        Logger::info(TAG, "Creating IPC structures...");

        ipc_ = std::make_unique<IpcManager>(Config::Ipc::SHM_KEY_BASE, true);
        Logger::info(TAG, "Station capacity set to ", TEST_STATION_CAPACITY);

        ipc_->initializeSemaphores(TEST_STATION_CAPACITY);

        simulationStartTime_ = time(nullptr);
        ipc_->initializeState(simulationStartTime_, simulationStartTime_ + 25);

        Logger::info(TAG, "IPC structures initialized");
    }

    void spawnProcesses() {
        Logger::info(TAG, "Spawning cashier...");
        cashierPid_ = ProcessSpawner::spawnWithKeys("cashier_process",
            ipc_->shmKey(), ipc_->semKey(), ipc_->cashierMsgKey());
        if (cashierPid_ > 0) {
            Logger::info(TAG, "Cashier spawned with PID ", cashierPid_);
        }
        ipc_->semaphores().wait(SemaphoreIndex::CASHIER_READY);
        Logger::info(TAG, "Cashier ready");

        Logger::info(TAG, "Spawning workers...");

        worker1Pid_ = ProcessSpawner::spawnWithKeys("worker1_process",
            ipc_->shmKey(), ipc_->semKey(), ipc_->msgKey());
        if (worker1Pid_ > 0) {
            Logger::info(TAG, "Worker1 spawned with PID ", worker1Pid_);
        }

        worker2Pid_ = ProcessSpawner::spawnWithKeys("worker2_process",
            ipc_->shmKey(), ipc_->semKey(), ipc_->msgKey());
        if (worker2Pid_ > 0) {
            Logger::info(TAG, "Worker2 spawned with PID ", worker2Pid_);
        }

        ipc_->semaphores().wait(SemaphoreIndex::WORKER1_READY);
        Logger::info(TAG, "Worker1 ready");
        ipc_->semaphores().wait(SemaphoreIndex::WORKER2_READY);
        Logger::info(TAG, "Worker2 ready");
    }

    void spawnTourists() {
        Logger::info(TAG, "Spawning tourists...");

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
            pid_t pid = spawnTourist(t);
            if (pid > 0) {
                touristPids_.push_back(pid);
            }
            usleep(Config::Timing::TOURIST_LOOP_POLL_US);
        }
    }

    pid_t spawnTourist(const TouristConfig& t) {
        return ProcessSpawner::spawn("tourist_process", {
            std::to_string(t.id),
            std::to_string(t.age),
            std::to_string(static_cast<int>(t.type)),
            std::to_string(t.isVip ? 1 : 0),
            std::to_string(t.wantsToRide ? 1 : 0),
            std::to_string(t.guardianId),
            std::to_string(static_cast<int>(t.trail)),
            std::to_string(ipc_->shmKey()),
            std::to_string(ipc_->semKey()),
            std::to_string(ipc_->msgKey()),
            std::to_string(ipc_->cashierMsgKey())
        });
    }

    void runSimulationLoop() {
        Logger::info(TAG, "Simulation running...");
        Logger::info(TAG, "Emergency stop scheduled at 8 seconds");
        Logger::info(TAG, "Resume scheduled at 13 seconds");

        time_t loopStartTime = time(nullptr);
        bool emergencyTriggered = false;
        bool resumeTriggered = false;

        while (!SignalHelper::shouldExit(signals_)) {
            time_t elapsed = time(nullptr) - loopStartTime;

            RopewayState currentState;
            {
                SemaphoreLock lock(ipc_->semaphores(), SemaphoreIndex::SHARED_MEMORY);
                currentState = ipc_->state()->core.state;
            }

            if (currentState == RopewayState::STOPPED) {
                Logger::info(TAG, "Ropeway stopped. Ending simulation.");
                break;
            }

            if (elapsed >= 8 && !emergencyTriggered) {
                Logger::info(TAG, ">>> TRIGGERING EMERGENCY STOP <<<");
                Logger::info(TAG, "Sending SIGUSR1 to Worker1 (PID: ", worker1Pid_, ")");
                if (worker1Pid_ > 0) {
                    kill(worker1Pid_, SIGUSR1);
                }
                emergencyTriggered = true;
            }

            if (elapsed >= 13 && emergencyTriggered && !resumeTriggered) {
                Logger::info(TAG, ">>> TRIGGERING RESUME <<<");
                Logger::info(TAG, "Sending SIGUSR2 to Worker1 (PID: ", worker1Pid_, ")");
                if (worker1Pid_ > 0) {
                    kill(worker1Pid_, SIGUSR2);
                }
                resumeTriggered = true;
            }

            if (elapsed >= 30) {
                Logger::info(TAG, "Timeout reached.");
                break;
            }

            usleep(Config::Timing::MAIN_LOOP_POLL_US);
        }
    }

    void generateReport() {
        time_t simulationEndTime = time(nullptr);

        std::stringstream report;
        {
            SemaphoreLock lock(ipc_->semaphores(), SemaphoreIndex::SHARED_MEMORY);
            ipc_->state()->stats.dailyStats.simulationEndTime = simulationEndTime;

            char startTimeStr[64], endTimeStr[64];
            struct tm* tm_start = localtime(&ipc_->state()->stats.dailyStats.simulationStartTime);
            strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%d %H:%M:%S", tm_start);
            struct tm* tm_end = localtime(&ipc_->state()->stats.dailyStats.simulationEndTime);
            strftime(endTimeStr, sizeof(endTimeStr), "%Y-%m-%d %H:%M:%S", tm_end);

            report << std::string(60, '=') << "\n";
            report << "           DAILY REPORT - ROPEWAY SIMULATION\n";
            report << std::string(60, '=') << "\n\n";

            report << "--- SIMULATION TIMING ---\n";
            report << "Start Time: " << startTimeStr << "\n";
            report << "End Time:   " << endTimeStr << "\n";
            report << "Duration:   " << (simulationEndTime - ipc_->state()->stats.dailyStats.simulationStartTime) << " seconds\n";

            report << "\n--- OVERALL STATISTICS ---\n";
            report << "Final State: " << EnumStrings::toString(ipc_->state()->core.state) << "\n";

            const DailyStatistics& dailyStats = ipc_->state()->stats.dailyStats;
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

            for (uint32_t i = 0; i < ipc_->state()->stats.touristRecordCount; ++i) {
                const TouristRideRecord& rec = ipc_->state()->stats.touristRecords[i];
                report << std::left << std::setw(10) << rec.touristId
                       << std::setw(10) << rec.ticketId
                       << std::setw(8) << rec.age
                       << std::setw(12) << EnumStrings::toString(rec.type)
                       << std::setw(6) << (rec.isVip ? "Yes" : "No")
                       << std::setw(8) << rec.ridesCompleted << "\n";
            }

            report << std::string(60, '=') << "\n";
        }

        std::string reportStr = report.str();
        write(STDOUT_FILENO, "\n", 1);
        write(STDOUT_FILENO, reportStr.c_str(), reportStr.length());

        std::string reportFilename = "daily_report_" + std::to_string(time(nullptr)) + ".txt";
        std::ofstream reportFile(reportFilename);
        if (reportFile.is_open()) {
            reportFile << reportStr;
            reportFile.close();
            Logger::info(TAG, "Daily report saved to: ", reportFilename.c_str(), "");
        }
    }

    void cleanup() {
        Logger::info(TAG, "Cleaning up processes...");

        ProcessSpawner::terminate(cashierPid_, "Cashier");
        ProcessSpawner::terminate(worker1Pid_, "Worker1");
        ProcessSpawner::terminate(worker2Pid_, "Worker2");
        ProcessSpawner::terminateAll(touristPids_);

        usleep(Config::Timing::PROCESS_CLEANUP_WAIT_US);
        ProcessSpawner::waitForAll();

        Logger::info(TAG, "Cleaning up IPC structures...");
        ipc_.reset();
        IpcManager::cleanup(Config::Ipc::SHM_KEY_BASE);
        Logger::info(TAG, "Done.");
    }

    std::unique_ptr<IpcManager> ipc_;
    SignalHelper::SignalFlags signals_;

    pid_t cashierPid_;
    pid_t worker1Pid_;
    pid_t worker2Pid_;
    std::vector<pid_t> touristPids_;

    time_t simulationStartTime_;
};
