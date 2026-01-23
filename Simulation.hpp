#pragma once

#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <memory>
#include <random>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>

#include "ipc/IpcManager.hpp"
#include "utils/SignalHelper.hpp"
#include "utils/ProcessSpawner.hpp"
#include "utils/Logger.hpp"
#include "structures/Tourist.hpp"
#include "ipc/core/IpcException.hpp"

/**
 * Main orchestrator for the ropeway simulation.
 * Manages IPC resources, spawns child processes, runs simulation loop,
 * and generates reports.
 */
class Simulation {
public:
    Simulation() {
        Logger::separator('=', 60);
        Logger::info(tag_, "Create Ropeway Simulation");
        Logger::separator('=', 60);

        SignalHelper::setup(signals_, SignalHelper::Mode::ORCHESTRATOR);
        IpcManager::cleanup(Config::Ipc::SHM_KEY_BASE);
    }

    ~Simulation() {
        cleanup();
    }

    void run() {
        try {
            initializeIpc();
            spawnProcesses();
            spawnTourists();
            runSimulationLoop();
            generateReport();
        } catch (const ipc_exception& e) {
            perror(e.what());
            // FIXME cleanup
            exit(1);
        } catch (const std::exception& e) {
            perror(e.what());
            // FIXME cleanup
            exit(1);
        }

    }

private:
    static constexpr auto tag_{"[Simulation] "};

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
        Logger::info(tag_, "Creating IPC structures...");

        ipc_ = std::make_unique<IpcManager>(Config::Ipc::SHM_KEY_BASE, true);
        Logger::info(tag_, "Station capacity set to ", Config::Simulation::STATION_CAPACITY);

        ipc_->initializeSemaphores(Config::Simulation::STATION_CAPACITY);

        simulationStartTime_ = time(nullptr);
        ipc_->initializeState(simulationStartTime_,
            simulationStartTime_ + Config::Simulation::DURATION_US / Config::Time::ONE_SECOND_US);

        Logger::info(tag_, "IPC structures initialized");
    }

    void spawnProcesses() {
        Logger::info(tag_, "Spawning cashier...");
        cashierPid_ = ProcessSpawner::spawnWithKeys("cashier_process",
            ipc_->shmKey(), ipc_->semKey(), ipc_->cashierMsgKey());
        if (cashierPid_ > 0) {
            Logger::info(tag_, "Cashier spawned with PID ", cashierPid_);
        }
        ipc_->semaphores().wait(Semaphore::Index::CASHIER_READY);
        Logger::info(tag_, "Cashier ready");

        Logger::info(tag_, "Spawning workers...");

        lowerWorkerPid_ = ProcessSpawner::spawnWithKeys("worker1_process",
            ipc_->shmKey(), ipc_->semKey(), ipc_->msgKey());
        if (lowerWorkerPid_ > 0) {
            Logger::info(tag_, "Worker1 spawned with PID ", lowerWorkerPid_);
        }

        upperWorkerPid_ = ProcessSpawner::spawnWithKeys("worker2_process",
            ipc_->shmKey(), ipc_->semKey(), ipc_->msgKey());
        if (upperWorkerPid_ > 0) {
            Logger::info(tag_, "Worker2 spawned with PID ", upperWorkerPid_);
        }

        ipc_->semaphores().wait(Semaphore::Index::LOWER_WORKER_READY, /* useUndo = */ false);
        Logger::info(tag_, "Worker1 ready");
        ipc_->semaphores().wait(Semaphore::Index::UPPER_WORKER_READY);
        Logger::info(tag_, "Worker2 ready");
    }

    void spawnTourists() {
        Logger::info(tag_, "Spawning tourists...");

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution ageDist(5, 75);
        std::uniform_int_distribution typeDist(0, 1);
        std::uniform_real_distribution vipDist(0.0, 1.0);
        std::uniform_real_distribution rideDist(0.0, 1.0);
        std::uniform_int_distribution trailDist(0, 2);

        constexpr uint32_t NUM_TOURISTS = Config::Simulation::NUM_TOURISTS;
        std::vector<TouristConfig> tourists;
        tourists.reserve(NUM_TOURISTS);

        for (uint32_t id = 1; id <= NUM_TOURISTS; ++id) {
            const uint32_t age = ageDist(gen);
            const TouristType type = (typeDist(gen) == 0) ? TouristType::PEDESTRIAN : TouristType::CYCLIST;
            const bool isVip = vipDist(gen) < Config::Vip::VIP_CHANCE_PERCENTAGE;
            const bool wantsToRide = rideDist(gen) > 0.1;
            const auto trail = static_cast<TrailDifficulty>(trailDist(gen));

            tourists.push_back({id, age, type, isVip, wantsToRide, -1, trail, ""});
        }

        Logger::info(tag_, "Spawning ", NUM_TOURISTS, " tourists...");
        for (const auto& t : tourists) {
            if (pid_t pid = spawnTourist(t); pid > 0) {
                touristPids_.push_back(pid);
            }
            usleep(Config::Time::ARRIVAL_DELAY_BASE_US + (gen() % Config::Time::ARRIVAL_DELAY_RANDOM_US));
        }
        Logger::info(tag_, "All tourists spawned");
    }

    pid_t spawnTourist(const TouristConfig& t) const {
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

    void runSimulationLoop() const {
        Logger::info(tag_, "Simulation running...");
        Logger::info(tag_, "Emergency stop scheduled at 8 seconds");
        Logger::info(tag_, "Resume scheduled at 13 seconds");

        const time_t loopStartTime = time(nullptr);
        bool emergencyTriggered = false;
        bool resumeTriggered = false;

        while (!SignalHelper::shouldExit(signals_)) {
            const time_t elapsed = time(nullptr) - loopStartTime;

            RopewayState currentState;
            {
                Semaphore::ScopedLock lock(ipc_->semaphores(), Semaphore::Index::SHARED_MEMORY);
                currentState = ipc_->state()->core.state;
            }

            if (currentState == RopewayState::STOPPED) {
                Logger::info(tag_, "Ropeway stopped. Ending simulation.");
                break;
            }

            if (elapsed >= 8 && !emergencyTriggered) {
                Logger::info(tag_, ">>> TRIGGERING EMERGENCY STOP <<<");
                Logger::info(tag_, "Sending SIGUSR1 to Worker1 (PID: ", lowerWorkerPid_, ")");
                if (lowerWorkerPid_ > 0) {
                    kill(lowerWorkerPid_, SIGUSR1);
                }
                emergencyTriggered = true;
            }

            if (elapsed >= 13 && emergencyTriggered && !resumeTriggered) {
                Logger::info(tag_, ">>> TRIGGERING RESUME <<<");
                Logger::info(tag_, "Sending SIGUSR2 to Worker1 (PID: ", lowerWorkerPid_, ")");
                if (lowerWorkerPid_ > 0) {
                    kill(lowerWorkerPid_, SIGUSR2);
                }
                resumeTriggered = true;
            }

            if (elapsed >= Config::Simulation::DURATION_US / Config::Time::ONE_SECOND_US) {
                Logger::info(tag_, "Timeout reached.");
                break;
            }

            usleep(Config::Time::MAIN_LOOP_POLL_US);
        }
    }

    void generateReport() const {
        time_t simulationEndTime = time(nullptr);

        std::stringstream report;
        {
            Semaphore::ScopedLock lock(ipc_->semaphores(), Semaphore::Index::SHARED_MEMORY);
            ipc_->state()->stats.dailyStats.simulationEndTime = simulationEndTime;

            char startTimeStr[64], endTimeStr[64];
            tm* tm_start = localtime(&ipc_->state()->stats.dailyStats.simulationStartTime);
            strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%d %H:%M:%S", tm_start);
            tm* tm_end = localtime(&ipc_->state()->stats.dailyStats.simulationEndTime);
            strftime(endTimeStr, sizeof(endTimeStr), "%Y-%m-%d %H:%M:%S", tm_end);

            report << std::string(60, '=') << "\n";
            report << "           DAILY REPORT - ROPEWAY SIMULATION\n";
            report << std::string(60, '=') << "\n\n";

            report << "--- SIMULATION TIMING ---\n";
            report << "Start Time: " << startTimeStr << "\n";
            report << "End Time:   " << endTimeStr << "\n";
            report << "Duration:   " << (simulationEndTime - ipc_->state()->stats.dailyStats.simulationStartTime) << " seconds\n";

            report << "\n--- OVERALL STATISTICS ---\n";
            report << "Final State: " << toString(ipc_->state()->core.state) << "\n";

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
                       << std::setw(12) << toString(rec.type)
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
            Logger::info(tag_, "Daily report saved to: ", reportFilename.c_str(), "");
        }
    }

    void cleanup() {
        Logger::info(tag_, "Cleaning up processes...");

        ProcessSpawner::terminate(cashierPid_, "Cashier");
        ProcessSpawner::terminate(lowerWorkerPid_, "Worker1");
        ProcessSpawner::terminate(upperWorkerPid_, "Worker2");
        ProcessSpawner::terminateAll(touristPids_);

        // Wait for ALL child processes to terminate (blocking)
        int status;
        while (waitpid(-1, &status, 0) > 0) {
            // Reap all children
        }

        Logger::info(tag_, "Cleaning up IPC structures...");
        ipc_.reset();
        IpcManager::cleanup(Config::Ipc::SHM_KEY_BASE);
        Logger::info(tag_, "Done.");
    }

    std::unique_ptr<IpcManager> ipc_;
    SignalHelper::SignalFlags signals_;

    pid_t cashierPid_{-1};
    pid_t lowerWorkerPid_{-1};
    pid_t upperWorkerPid_{-1};
    std::vector<pid_t> touristPids_;

    time_t simulationStartTime_;
};
