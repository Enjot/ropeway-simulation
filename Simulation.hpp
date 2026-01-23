#pragma once

#include <vector>
#include <random>
#include <memory>
#include <sys/wait.h>
#include <csignal>

#include "ipc/IpcManager.hpp"
#include "utils/SignalHelper.hpp"
#include "utils/ProcessSpawner.hpp"
#include "utils/Logger.hpp"

class Simulation {
public:
    void run() {
        Logger::separator('=');
        Logger::info(tag_, "Ropeway Simulation");
        Logger::separator('=');

        SignalHelper::setup(signals_, SignalHelper::Mode::ORCHESTRATOR);

        try {
            setup();
            // mainLoop();
        } catch (const std::exception& e) {
            Logger::error(tag_, "Exception: %s", e.what());
        }

        shutdown();
    }

private:
    static constexpr auto tag_{"Simulation"};

    std::unique_ptr<IpcManager> ipc_;
    SignalHelper::SignalFlags signals_;

    pid_t cashierPid_{-1};
    pid_t lowerWorkerPid_{-1};
    pid_t upperWorkerPid_{-1};
    std::vector<pid_t> touristPids_;

    time_t startTime_;

    void setup() {
        Logger::info(tag_, "Creating IPC...");
        ipc_ = std::make_unique<IpcManager>();

        ipc_->initSemaphores(Config::Simulation::STATION_CAPACITY);

        startTime_ = time(nullptr);
        const time_t endTime = startTime_ + Config::Simulation::DURATION_US / Config::Time::ONE_SECOND_US;
        ipc_->initState(startTime_, endTime);

        Logger::debug(tag_, "Spawning processes...");
        spawnWorkers();
        // spawnCashier();
        // waitForReady();
    }

    void spawnWorkers() {
        lowerWorkerPid_ = ProcessSpawner::spawnWithKeys("lower_worker_process",
        ipc_->shmKey(), ipc_->semKey(), ipc_->workerMsgKey());
        upperWorkerPid_ = ProcessSpawner::spawnWithKeys("upper_worker_process",
            ipc_->shmKey(), ipc_->semKey(), ipc_->workerMsgKey());
        Logger::debug(tag_, "Workers spawned: %d, %d", lowerWorkerPid_, upperWorkerPid_);
    }

    void spawnCashier() {
        cashierPid_ = ProcessSpawner::spawnWithKeys("cashier_process",
            ipc_->shmKey(), ipc_->semKey(), ipc_->cashierMsgKey());
        Logger::debug(tag_, "Cashier spawned: %d", cashierPid_);
    }

    void waitForReady() {
        ipc_->sem().wait(Semaphore::Index::CASHIER_READY);
        ipc_->sem().wait(Semaphore::Index::LOWER_WORKER_READY, false);
        ipc_->sem().wait(Semaphore::Index::UPPER_WORKER_READY);
        Logger::info(tag_, "All processes ready");
    }

    void mainLoop() {
        Logger::debug(tag_, "Spawning tourists...");
        spawnTourists();

        Logger::debug(tag_, "Running simulation...");
        while (!SignalHelper::shouldExit(signals_)) {
            time_t elapsed = time(nullptr) - startTime_;

            if (elapsed >= Config::Simulation::DURATION_US / Config::Time::ONE_SECOND_US) {
                Logger::info(tag_, "Time limit reached");
                break;
            }

            Semaphore::ScopedLock lock(ipc_->sem(), Semaphore::Index::SHARED_MEMORY);
            if (ipc_->state()->core.state == RopewayState::STOPPED) {
                Logger::info(tag_, "Ropeway stopped");
                break;
            }

            usleep(Config::Time::MAIN_LOOP_POLL_US);
        }
    }

    void spawnTourists() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution ageDist(5, 75);
        std::uniform_int_distribution typeDist(0, 1);
        std::uniform_real_distribution<> vipDist(0.0, 1.0);
        std::uniform_real_distribution<> rideDist(0.0, 1.0);
        std::uniform_int_distribution trailDist(0, 2);

        for (uint32_t id = 1; id <= Config::Simulation::NUM_TOURISTS; ++id) {
            pid_t pid = ProcessSpawner::spawn("tourist_process", {
                std::to_string(id),
                std::to_string(ageDist(gen)),
                std::to_string(typeDist(gen)),
                std::to_string(vipDist(gen) < Config::Vip::VIP_CHANCE_PERCENTAGE ? 1 : 0),
                std::to_string(rideDist(gen) > 0.1 ? 1 : 0),
                std::to_string(-1),  // guardianId
                std::to_string(trailDist(gen)),
                std::to_string(ipc_->shmKey()),
                std::to_string(ipc_->semKey()),
                std::to_string(ipc_->workerMsgKey()),
                std::to_string(ipc_->cashierMsgKey())
            });

            if (pid > 0) touristPids_.push_back(pid);

            usleep(Config::Time::ARRIVAL_DELAY_BASE_US + (gen() % Config::Time::ARRIVAL_DELAY_RANDOM_US));
        }
        Logger::info(tag_, "Spawned %d tourists", static_cast<int>(touristPids_.size()));
    }

    void shutdown() {
        Logger::debug(tag_, "Shutting down...");

        // Terminate processes
        ProcessSpawner::terminate(cashierPid_, "Cashier");
        ProcessSpawner::terminate(lowerWorkerPid_, "LowerWorker");
        ProcessSpawner::terminate(upperWorkerPid_, "UpperWorker");
        ProcessSpawner::terminateAll(touristPids_);

        // Wait for all children
        while (waitpid(-1, nullptr, 0) > 0) {}

        // IpcManager cleans up automatically (isOwner_)
        ipc_.reset();

        Logger::debug(tag_, "Done");
    }
};
