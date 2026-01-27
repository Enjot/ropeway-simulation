#pragma once

#include <vector>
#include <random>
#include <memory>
#include <sys/wait.h>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

#include "ipc/IpcManager.h"
#include "core/Config.h"
#include "utils/SignalHelper.h"
#include "utils/ProcessSpawner.h"
#include "logging/Logger.h"
#include "utils/TimeHelper.h"

class Simulation {
public:
    void run() {
        Logger::separator('=');
        Logger::info(tag_, "Ropeway Simulation");
        Logger::separator('=');

        SignalHelper::setup(signals_, false);
        SignalHelper::ignoreChildren();

        try {
            setup();
            mainLoop();
        } catch (const std::exception &e) {
            Logger::error(tag_, "Exception: %s", e.what());
        }

        shutdown();
    }

private:
    static constexpr auto tag_{"Simulation"};

    std::unique_ptr<IpcManager> ipc_;
    SignalHelper::Flags signals_;

    pid_t loggerPid_{-1};
    pid_t cashierPid_{-1};
    pid_t lowerWorkerPid_{-1};
    pid_t upperWorkerPid_{-1};
    std::vector<pid_t> touristPids_;

    time_t startTime_;

    void setup() {
        Logger::info(tag_, "Creating IPC...");
        ipc_ = std::make_unique<IpcManager>();

        ipc_->initSemaphores(Config::Simulation::STATION_CAPACITY());

        startTime_ = time(nullptr);
        const time_t endTime = startTime_ + Config::Simulation::DURATION_US() / Config::Time::ONE_SECOND_US();
        ipc_->initState(startTime_, endTime);

        // Set simulation start time for logger
        Logger::setSimulationStartTime(startTime_);

        Logger::debug(tag_, "Spawning processes...");
        spawnLogger();
        spawnWorkers();
        spawnCashier();
        waitForReady();
    }

    void spawnLogger() {
        loggerPid_ = ProcessSpawner::spawnWithKeys("logger_process",
                                                   ipc_->shmKey(), ipc_->semKey(), ipc_->logMsgKey());
        Logger::debug(tag_, "Logger spawned: %d", loggerPid_);
        usleep(50000); // Brief delay to ensure logger is ready

        // Switch to centralized logging (from now on, logs go through the logger process)
        Logger::initCentralized(ipc_->shmKey(), ipc_->semKey(), ipc_->logMsgKey());
    }

    void spawnWorkers() {
        lowerWorkerPid_ = ProcessSpawner::spawnWithKeys("lower_worker_process",
                                                        ipc_->shmKey(), ipc_->semKey(), ipc_->workerMsgKey(),
                                                        ipc_->entryGateMsgKey(), ipc_->logMsgKey());
        upperWorkerPid_ = ProcessSpawner::spawnWithKeys("upper_worker_process",
                                                        ipc_->shmKey(), ipc_->semKey(), ipc_->workerMsgKey(),
                                                        ipc_->entryGateMsgKey(), ipc_->logMsgKey());
        Logger::debug(tag_, "Workers spawned: %d, %d", lowerWorkerPid_, upperWorkerPid_);
    }

    void spawnCashier() {
        cashierPid_ = ProcessSpawner::spawnWithKeys("cashier_process",
                                                    ipc_->shmKey(), ipc_->semKey(), ipc_->cashierMsgKey(),
                                                    ipc_->logMsgKey());
        Logger::debug(tag_, "Cashier spawned: %d", cashierPid_);
    }

    void waitForReady() {
        while (!signals_.exit) {
            if (!ipc_->sem().wait(Semaphore::Index::CASHIER_READY)) continue;
            break;
        }
        while (!signals_.exit) {
            if (!ipc_->sem().wait(Semaphore::Index::LOWER_WORKER_READY, false)) continue;
            break;
        }
        while (!signals_.exit) {
            if (!ipc_->sem().wait(Semaphore::Index::UPPER_WORKER_READY)) continue;
            break;
        }
        if (!signals_.exit) {
            Logger::info(tag_, "All processes ready");
        }
    }

    void mainLoop() {
        Logger::debug(tag_, "Spawning tourists...");
        spawnTourists();

        Logger::debug(tag_, "Running simulation...");
        bool closingTimeReached = false;
        time_t drainStartTime = 0;

        while (!signals_.exit) {
            time_t now = time(nullptr);
            time_t elapsed = now - startTime_;

            // Calculate simulated time
            uint32_t simSeconds = Config::Simulation::OPENING_HOUR() * 3600 +
                                  static_cast<uint32_t>(elapsed) * Config::Simulation::TIME_SCALE();
            uint32_t simHour = simSeconds / 3600;

            // Check for closing time (Tk)
            if (!closingTimeReached && simHour >= Config::Simulation::CLOSING_HOUR()) {
                closingTimeReached = true;
                Logger::warn(tag_, ">>> CLOSING TIME REACHED (Tk=%u:00) - Gates stop accepting <<<",
                             Config::Simulation::CLOSING_HOUR());

                Semaphore::ScopedLock lock(ipc_->sem(), Semaphore::Index::SHM_OPERATIONAL);
                ipc_->state()->operational.acceptingNewTourists = false;
                ipc_->state()->operational.state = RopewayState::CLOSING;
            }

            // After closing, wait for tourists to drain then shutdown
            if (closingTimeReached) {
                uint32_t touristsInStation, chairsInUse;
                {
                    // Lock ordering: SHM_OPERATIONAL first, then SHM_CHAIRS
                    Semaphore::ScopedLock lockCore(ipc_->sem(), Semaphore::Index::SHM_OPERATIONAL);
                    Semaphore::ScopedLock lockChairs(ipc_->sem(), Semaphore::Index::SHM_CHAIRS);
                    touristsInStation = ipc_->state()->operational.touristsInLowerStation;
                    chairsInUse = ipc_->state()->chairPool.chairsInUse;
                }

                if (touristsInStation == 0 && chairsInUse == 0) {
                    if (drainStartTime == 0) {
                        drainStartTime = now;
                        Logger::info(tag_, "Station empty, waiting %u seconds before shutdown...",
                                     Constants::Ropeway::SHUTDOWN_DELAY_SEC * Config::Time::ONE_SECOND_US() / Config::Time::ONE_SECOND_US());
                    }

                    // Wait 3 seconds after station is empty
                    if (now - drainStartTime >= Constants::Ropeway::SHUTDOWN_DELAY_SEC * Config::Time::ONE_SECOND_US() / Config::Time::ONE_SECOND_US()) {
                        Logger::info(tag_, "Shutdown delay complete, stopping ropeway");
                        {
                            Semaphore::ScopedLock lock(ipc_->sem(), Semaphore::Index::SHM_OPERATIONAL);
                            ipc_->state()->operational.state = RopewayState::STOPPED;
                        }
                        break;
                    }
                } else {
                    // Reset drain timer if new tourists appear
                    drainStartTime = 0;
                    static time_t lastDrainLog = 0;
                    if (now - lastDrainLog >= 2) {
                        Logger::info(tag_, "Draining: %u in station, %u chairs in use",
                                     touristsInStation, chairsInUse);
                        lastDrainLog = now;
                    }
                }
            }

            // Emergency stop is now handled autonomously by LowerStationWorker
            // (detects "danger" randomly and initiates stop/resume protocol)

            {
                Semaphore::ScopedLock lock(ipc_->sem(), Semaphore::Index::SHM_OPERATIONAL);
                if (ipc_->state()->operational.state == RopewayState::STOPPED) {
                    Logger::info(tag_, "Ropeway stopped");
                    break;
                }
            }

            usleep(Config::Time::MAIN_LOOP_POLL_US());
        }
    }

    void spawnTourists() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution ageDist(18, 75);
        std::uniform_int_distribution typeDist(0, 1);
        std::uniform_real_distribution<> vipDist(0.0, 1.0);
        std::uniform_real_distribution<> rideDist(0.0, 1.0);
        std::uniform_int_distribution trailDist(0, 2);

        // Initialize nextTouristId counter
        {
            Semaphore::ScopedLock lock(ipc_->sem(), Semaphore::Index::SHM_STATS);
            ipc_->state()->stats.nextTouristId = Config::Simulation::NUM_TOURISTS();
        }

        for (uint32_t id = 1; id <= Config::Simulation::NUM_TOURISTS(); ++id) {
            uint32_t age = ageDist(gen);
            bool wantsToRide = (rideDist(gen) > 0.1);

            // Tourist process handles children/bike internally as threads
            pid_t pid = ProcessSpawner::spawn("tourist_process", {
                std::to_string(id),
                std::to_string(age),
                std::to_string(typeDist(gen)),
                std::to_string(vipDist(gen) < Constants::Vip::VIP_CHANCE ? 1 : 0),
                std::to_string(wantsToRide ? 1 : 0),
                std::to_string(trailDist(gen)),
                std::to_string(ipc_->shmKey()),
                std::to_string(ipc_->semKey()),
                std::to_string(ipc_->workerMsgKey()),
                std::to_string(ipc_->cashierMsgKey()),
                std::to_string(ipc_->entryGateMsgKey()),
                std::to_string(ipc_->logMsgKey())
            });

            if (pid > 0) touristPids_.push_back(pid);

            usleep(Config::Time::ARRIVAL_DELAY_BASE_US() + (gen() % Config::Time::ARRIVAL_DELAY_RANDOM_US()));
        }
        Logger::info(tag_, "Spawned %d tourists", static_cast<int>(touristPids_.size()));
    }

    void shutdown() {
        Logger::debug(tag_, "Shutting down...");

        // Generate end-of-day report before cleanup
        generateDailyReport();

        // Stop using centralized logging before terminating logger
        Logger::cleanupCentralized();

        // Terminate processes (logger last to capture final messages)
        ProcessSpawner::terminate(cashierPid_, "Cashier");
        ProcessSpawner::terminate(lowerWorkerPid_, "LowerWorker");
        ProcessSpawner::terminate(upperWorkerPid_, "UpperWorker");
        ProcessSpawner::terminateAll(touristPids_);
        usleep(100000); // Brief delay to let logger process remaining messages
        ProcessSpawner::terminate(loggerPid_, "Logger");

        // Wait for all children
        while (waitpid(-1, nullptr, 0) > 0) {
        }

        // IpcManager cleans up automatically (isOwner_)
        ipc_.reset();

        Logger::debug(tag_, "Done");
    }

    /** Helper: write formatted string to file descriptor using POSIX write() */
    static void writeToFd(int fd, const char* format, ...) {
        char buf[256];
        va_list args;
        va_start(args, format);
        int len = vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        if (len > 0) {
            write(fd, buf, len);
        }
    }

    void generateDailyReport() {
        Logger::info(tag_, "Generating end-of-day report...");

        // POSIX file operations: open(), write(), close()
        int fd = open("daily_report.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            perror("open daily_report.txt");
            Logger::error(tag_, "Failed to create report file");
            return;
        }

        const auto &state = *ipc_->state();
        const auto &stats = state.stats.dailyStats;
        uint32_t adultsServed = stats.totalTourists > (stats.childrenServed + stats.seniorsServed)
                                    ? stats.totalTourists - stats.childrenServed - stats.seniorsServed
                                    : 0;

        writeToFd(fd, "ROPEWAY DAILY REPORT\n");
        writeToFd(fd, "====================\n");
        writeToFd(fd, "Operating hours: %02u:00 - %02u:00\n\n",
                Config::Simulation::OPENING_HOUR(), Config::Simulation::CLOSING_HOUR());

        writeToFd(fd, "FINANCIAL\n");
        writeToFd(fd, "  Revenue:        %.2f PLN\n", stats.totalRevenueWithDiscounts);
        writeToFd(fd, "  Tickets sold:   %u\n\n", stats.ticketsSold);

        writeToFd(fd, "TOURISTS (%u total)\n", stats.totalTourists);
        writeToFd(fd, "  Children (<10): %u\n", stats.childrenServed);
        writeToFd(fd, "  Adults (10-64): %u\n", adultsServed);
        writeToFd(fd, "  Seniors (65+):  %u\n", stats.seniorsServed);
        writeToFd(fd, "  VIP:            %u\n\n", stats.vipTourists);

        writeToFd(fd, "TYPES\n");
        writeToFd(fd, "  Pedestrians:    %u\n", stats.pedestrianRides);
        writeToFd(fd, "  Cyclists:       %u\n\n", stats.cyclistRides);

        writeToFd(fd, "RIDES\n");
        writeToFd(fd, "  Total rides:    %u\n", state.operational.totalRidesToday);
        writeToFd(fd, "  Gate passages:  %u\n", state.stats.gateLog.count);

        if (stats.emergencyStops > 0) {
            writeToFd(fd, "\nEMERGENCY\n");
            writeToFd(fd, "  Stops:          %u\n", stats.emergencyStops);
        }

        // Per-tourist/ticket ride counts (required by specification)
        writeToFd(fd, "\nRIDES PER TOURIST/TICKET\n");
        writeToFd(fd, "%-10s %-10s %-5s %-10s %-6s %-8s %-8s\n",
                "Tourist", "Ticket", "Age", "Type", "VIP", "Rides", "Guardian");
        writeToFd(fd, "--------------------------------------------------------------\n");

        for (uint32_t i = 0; i < state.stats.touristRecordCount; ++i) {
            const auto &record = state.stats.touristRecords[i];
            writeToFd(fd, "%-10u %-10u %-5u %-10s %-6s %-8u %-8d\n",
                    record.touristId,
                    record.ticketId,
                    record.age,
                    record.type == TouristType::CYCLIST ? "Cyclist" : "Pedestrian",
                    record.isVip ? "Yes" : "No",
                    record.ridesCompleted,
                    record.guardianId);
        }

        // Gate passage log (required: "przejście przez bramki jest rejestrowane")
        writeToFd(fd, "\nGATE PASSAGE LOG\n");
        writeToFd(fd, "%-8s %-10s %-10s %-6s %-8s %-8s\n",
                "Time", "Tourist", "Ticket", "Gate", "Type", "Allowed");
        writeToFd(fd, "--------------------------------------------------------------\n");

        for (uint32_t i = 0; i < state.stats.gateLog.count; ++i) {
            const auto &passage = state.stats.gateLog.entries[i];
            char timeStr[6];
            passage.formatSimTime(timeStr);
            writeToFd(fd, "%-8s %-10u %-10u %-6u %-8s %-8s\n",
                    timeStr,
                    passage.touristId,
                    passage.ticketId,
                    passage.gateNumber,
                    passage.gateType == GateType::ENTRY ? "Entry" : "Ride",
                    passage.wasAllowed ? "Yes" : "No");
        }

        close(fd);
        Logger::info(tag_, "Report saved to daily_report.txt");
    }
};
