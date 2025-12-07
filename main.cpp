#include <iostream>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <cstring>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "ipc/SharedMemory.hpp"
#include "ipc/Semaphore.hpp"
#include "ipc/MessageQueue.hpp"
#include "ipc/ropeway_system_state.hpp"
#include "ipc/worker_message.hpp"
#include "ipc/semaphore_index.hpp"
#include "structures/tourist.hpp"
#include "common/config.hpp"

namespace {
    constexpr key_t SHM_KEY = Config::Ipc::SHM_KEY_BASE;
    constexpr key_t SEM_KEY = Config::Ipc::SEM_KEY_BASE;
    constexpr key_t MSG_KEY = Config::Ipc::MSG_KEY_BASE;

    volatile sig_atomic_t g_shouldExit = 0;

    void signalHandler(int signum) {
        if (signum == SIGINT || signum == SIGTERM) {
            g_shouldExit = 1;
        }
    }

    void setupSignalHandlers() {
        struct sigaction sa{};
        sa.sa_handler = signalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
    }

    void cleanupIpc() {
        SharedMemory<RopewaySystemState>::removeByKey(SHM_KEY);
        Semaphore::removeByKey(SEM_KEY);
        MessageQueue<WorkerMessage>::removeByKey(MSG_KEY);
    }

    std::string getTouristProcessPath() {
        // Get the directory of the current executable
        char path[1024];
        uint32_t size = sizeof(path);

        #ifdef __APPLE__
        if (_NSGetExecutablePath(path, &size) != 0) {
            return "./tourist_process";
        }
        #else
        ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (len == -1) {
            return "./tourist_process";
        }
        path[len] = '\0';
        #endif

        // Find last slash and replace executable name
        char* lastSlash = strrchr(path, '/');
        if (lastSlash != nullptr) {
            strcpy(lastSlash + 1, "tourist_process");
            return path;
        }
        return "./tourist_process";
    }

    pid_t spawnTourist(uint32_t id, uint32_t age, TouristType type, bool isVip,
                       bool wantsToRide, int32_t guardianId, TrailDifficulty trail) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            return -1;
        }

        if (pid == 0) {
            // Child process - exec tourist_process
            std::string processPath = getTouristProcessPath();

            char idStr[16], ageStr[16], typeStr[16], vipStr[16], rideStr[16];
            char guardianStr[16], trailStr[16], shmStr[16], semStr[16], msgStr[16];

            snprintf(idStr, sizeof(idStr), "%u", id);
            snprintf(ageStr, sizeof(ageStr), "%u", age);
            snprintf(typeStr, sizeof(typeStr), "%d", static_cast<int>(type));
            snprintf(vipStr, sizeof(vipStr), "%d", isVip ? 1 : 0);
            snprintf(rideStr, sizeof(rideStr), "%d", wantsToRide ? 1 : 0);
            snprintf(guardianStr, sizeof(guardianStr), "%d", guardianId);
            snprintf(trailStr, sizeof(trailStr), "%d", static_cast<int>(trail));
            snprintf(shmStr, sizeof(shmStr), "%d", SHM_KEY);
            snprintf(semStr, sizeof(semStr), "%d", SEM_KEY);
            snprintf(msgStr, sizeof(msgStr), "%d", MSG_KEY);

            execl(processPath.c_str(), "tourist_process",
                  idStr, ageStr, typeStr, vipStr, rideStr,
                  guardianStr, trailStr, shmStr, semStr, msgStr,
                  nullptr);

            // If exec fails
            perror("execl");
            _exit(1);
        }

        return pid;
    }
}

int main() {
    std::cout << "Ropeway Simulation - Tourist Process Test\n" << std::endl;

    setupSignalHandlers();
    cleanupIpc();

    try {
        // Create IPC structures
        std::cout << "Creating IPC structures..." << std::endl;

        SharedMemory<RopewaySystemState> shm(SHM_KEY, true);
        Semaphore sem(SEM_KEY, SemaphoreIndex::TOTAL_SEMAPHORES, true);
        MessageQueue<WorkerMessage> msgQueue(MSG_KEY, true);

        // Initialize semaphores
        sem.setValue(SemaphoreIndex::STATION_CAPACITY, Config::Gate::MAX_TOURISTS_ON_STATION);
        sem.setValue(SemaphoreIndex::SHARED_MEMORY, 1);
        sem.setValue(SemaphoreIndex::ENTRY_GATES, Config::Gate::NUM_ENTRY_GATES);
        sem.setValue(SemaphoreIndex::RIDE_GATES, Config::Gate::NUM_RIDE_GATES);
        sem.setValue(SemaphoreIndex::CHAIR_ALLOCATION, 1);
        sem.setValue(SemaphoreIndex::WORKER_SYNC, 0);

        // Initialize shared state
        shm->state = RopewayState::RUNNING;
        shm->acceptingNewTourists = true;
        shm->openingTime = time(nullptr);
        shm->closingTime = time(nullptr) + 30; // 30 seconds simulation

        std::cout << "IPC structures created and initialized" << std::endl;
        std::cout << "Station capacity: " << Config::Gate::MAX_TOURISTS_ON_STATION << std::endl;

        // Spawn tourists
        std::vector<pid_t> touristPids;

        std::cout << "\nSpawning tourists..." << std::endl;

        // Spawn a few test tourists
        struct TouristConfig {
            uint32_t id;
            uint32_t age;
            TouristType type;
            bool isVip;
            bool wantsToRide;
            int32_t guardianId;
            TrailDifficulty trail;
        };

        std::vector<TouristConfig> tourists = {
            {1, 25, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY},
            {2, 30, TouristType::CYCLIST, false, true, -1, TrailDifficulty::MEDIUM},
            {3, 7, TouristType::PEDESTRIAN, false, true, 4, TrailDifficulty::EASY},  // Child with guardian
            {4, 35, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY}, // Guardian
            {5, 70, TouristType::PEDESTRIAN, true, true, -1, TrailDifficulty::EASY},  // VIP senior
        };

        for (const auto& t : tourists) {
            pid_t pid = spawnTourist(t.id, t.age, t.type, t.isVip, t.wantsToRide, t.guardianId, t.trail);
            if (pid > 0) {
                touristPids.push_back(pid);
                std::cout << "Spawned tourist " << t.id << " with PID " << pid << std::endl;
            }
            usleep(100000); // 100ms between spawns
        }

        std::cout << "\nWaiting for tourists to complete..." << std::endl;

        // Wait for tourists (with timeout)
        int activeCount = static_cast<int>(touristPids.size());
        time_t startTime = time(nullptr);
        constexpr int TIMEOUT_S = 20;

        while (activeCount > 0 && !g_shouldExit) {
            int status;
            pid_t finishedPid = waitpid(-1, &status, WNOHANG);

            if (finishedPid > 0) {
                activeCount--;
                if (WIFEXITED(status)) {
                    std::cout << "Tourist (PID " << finishedPid << ") exited with status "
                              << WEXITSTATUS(status) << std::endl;
                } else if (WIFSIGNALED(status)) {
                    std::cout << "Tourist (PID " << finishedPid << ") killed by signal "
                              << WTERMSIG(status) << std::endl;
                }
            }

            if (time(nullptr) - startTime > TIMEOUT_S) {
                std::cout << "Timeout reached, stopping simulation..." << std::endl;
                shm->acceptingNewTourists = false;

                // Send SIGTERM to remaining tourists
                for (pid_t pid : touristPids) {
                    kill(pid, SIGTERM);
                }
                break;
            }

            usleep(100000); // 100ms
        }

        // Final wait for any remaining processes
        while (waitpid(-1, nullptr, WNOHANG) > 0) {}

        // Print final statistics
        std::cout << "\n=== Simulation Statistics ===" << std::endl;
        std::cout << "Total rides today: " << shm->totalRidesToday << std::endl;
        std::cout << "Tourists still on station: " << shm->touristsInLowerStation << std::endl;
        std::cout << "Tourists on platform: " << shm->touristsOnPlatform << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        cleanupIpc();
        return 1;
    }

    std::cout << "\nCleaning up IPC structures..." << std::endl;
    cleanupIpc();
    std::cout << "Done." << std::endl;

    return 0;
}
