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
#include "ipc/cashier_message.hpp"
#include "ipc/semaphore_index.hpp"
#include "structures/tourist.hpp"
#include "common/config.hpp"

namespace {
    constexpr key_t SHM_KEY = Config::Ipc::SHM_KEY_BASE;
    constexpr key_t SEM_KEY = Config::Ipc::SEM_KEY_BASE;
    constexpr key_t MSG_KEY = Config::Ipc::MSG_KEY_BASE;
    constexpr key_t CASHIER_MSG_KEY = Config::Ipc::MSG_KEY_BASE + 1;

    volatile sig_atomic_t g_shouldExit = 0;
    pid_t g_worker1Pid = 0;
    pid_t g_worker2Pid = 0;
    pid_t g_cashierPid = 0;

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

        signal(SIGCHLD, SIG_IGN);
    }

    void cleanupIpc() {
        SharedMemory<RopewaySystemState>::removeByKey(SHM_KEY);
        Semaphore::removeByKey(SEM_KEY);
        MessageQueue<WorkerMessage>::removeByKey(MSG_KEY);
        MessageQueue<TicketRequest>::removeByKey(CASHIER_MSG_KEY);
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
            snprintf(shmStr, sizeof(shmStr), "%d", SHM_KEY);
            snprintf(semStr, sizeof(semStr), "%d", SEM_KEY);
            snprintf(msgStr, sizeof(msgStr), "%d", MSG_KEY);

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
            snprintf(shmStr, sizeof(shmStr), "%d", SHM_KEY);
            snprintf(semStr, sizeof(semStr), "%d", SEM_KEY);
            snprintf(cashierMsgStr, sizeof(cashierMsgStr), "%d", CASHIER_MSG_KEY);

            execl(processPath.c_str(), "cashier_process", shmStr, semStr, cashierMsgStr, nullptr);

            perror("execl");
            _exit(1);
        }

        return pid;
    }

    pid_t spawnTourist(uint32_t id, uint32_t age, TouristType type, bool isVip,
                       bool wantsToRide, int32_t guardianId, TrailDifficulty trail) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            return -1;
        }

        if (pid == 0) {
            std::string processPath = getProcessPath("tourist_process");

            char idStr[16], ageStr[16], typeStr[16], vipStr[16], rideStr[16];
            char guardianStr[16], trailStr[16], shmStr[16], semStr[16], msgStr[16], cashierMsgStr[16];

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
            snprintf(cashierMsgStr, sizeof(cashierMsgStr), "%d", CASHIER_MSG_KEY);

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
            std::cout << "Terminating " << name << " (PID: " << pid << ")" << std::endl;
            kill(pid, SIGTERM);
            usleep(100000);
            kill(pid, SIGKILL);
        }
    }
}

int main() {
    std::cout << "=== Ropeway Simulation - Full System Test ===" << std::endl;
    std::cout << "Testing: Workers + Cashier + Tourists + Discounts\n" << std::endl;

    setupSignalHandlers();
    cleanupIpc();

    try {
        // Create IPC structures
        std::cout << "[Main] Creating IPC structures..." << std::endl;

        SharedMemory<RopewaySystemState> shm(SHM_KEY, true);
        Semaphore sem(SEM_KEY, SemaphoreIndex::TOTAL_SEMAPHORES, true);
        MessageQueue<WorkerMessage> workerMsgQueue(MSG_KEY, true);
        MessageQueue<TicketRequest> cashierMsgQueue(CASHIER_MSG_KEY, true);

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
        shm->closingTime = time(nullptr) + 20; // 20 seconds simulation

        std::cout << "[Main] IPC structures initialized" << std::endl;

        // Spawn Cashier first
        std::cout << "\n[Main] Spawning cashier..." << std::endl;
        g_cashierPid = spawnCashier();
        if (g_cashierPid > 0) {
            std::cout << "[Main] Cashier spawned with PID " << g_cashierPid << std::endl;
        }
        usleep(100000); // Let cashier initialize

        // Spawn workers
        std::cout << "\n[Main] Spawning workers..." << std::endl;

        g_worker1Pid = spawnWorker("worker1_process");
        if (g_worker1Pid > 0) {
            std::cout << "[Main] Worker1 spawned with PID " << g_worker1Pid << std::endl;
        }

        g_worker2Pid = spawnWorker("worker2_process");
        if (g_worker2Pid > 0) {
            std::cout << "[Main] Worker2 spawned with PID " << g_worker2Pid << std::endl;
        }

        usleep(200000);

        // Spawn tourists with various ages to test discounts
        std::vector<pid_t> touristPids;
        std::cout << "\n[Main] Spawning tourists (testing discounts)..." << std::endl;

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
            {1, 8, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY, "Child (8yo, 25% discount)"},
            {2, 70, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY, "Senior (70yo, 25% discount)"},
            {3, 30, TouristType::CYCLIST, false, true, -1, TrailDifficulty::MEDIUM, "Adult cyclist (no discount)"},
            {4, 25, TouristType::PEDESTRIAN, true, true, -1, TrailDifficulty::EASY, "Adult VIP request"},
            {5, 5, TouristType::PEDESTRIAN, false, true, 3, TrailDifficulty::EASY, "Young child (5yo, needs guardian)"},
        };

        for (const auto& t : tourists) {
            std::cout << "[Main] Spawning Tourist " << t.id << ": " << t.description << std::endl;
            pid_t pid = spawnTourist(t.id, t.age, t.type, t.isVip, t.wantsToRide, t.guardianId, t.trail);
            if (pid > 0) {
                touristPids.push_back(pid);
            }
            usleep(150000); // Stagger spawns
        }

        // Main simulation loop
        std::cout << "\n[Main] Simulation running..." << std::endl;

        time_t startTime = time(nullptr);

        while (!g_shouldExit) {
            time_t elapsed = time(nullptr) - startTime;

            // Check system state
            RopewayState currentState;
            {
                SemaphoreLock lock(sem, SemaphoreIndex::SHARED_MEMORY);
                currentState = shm->state;
            }

            if (currentState == RopewayState::STOPPED) {
                std::cout << "\n[Main] Ropeway stopped. Ending simulation." << std::endl;
                break;
            }

            // Timeout after 25 seconds
            if (elapsed >= 25) {
                std::cout << "\n[Main] Timeout reached." << std::endl;
                break;
            }

            usleep(500000); // 500ms
        }

        // Print final statistics
        std::cout << "\n=== Final Simulation Statistics ===" << std::endl;
        {
            SemaphoreLock lock(sem, SemaphoreIndex::SHARED_MEMORY);
            std::cout << "State: ";
            switch (shm->state) {
                case RopewayState::STOPPED: std::cout << "STOPPED"; break;
                case RopewayState::RUNNING: std::cout << "RUNNING"; break;
                case RopewayState::EMERGENCY_STOP: std::cout << "EMERGENCY_STOP"; break;
                case RopewayState::CLOSING: std::cout << "CLOSING"; break;
            }
            std::cout << std::endl;
            std::cout << "Total rides today: " << shm->totalRidesToday << std::endl;
            std::cout << "Tourists in station: " << shm->touristsInLowerStation << std::endl;
            std::cout << "Tourists on platform: " << shm->touristsOnPlatform << std::endl;
        }

        // Cleanup
        std::cout << "\n[Main] Cleaning up processes..." << std::endl;

        terminateProcess(g_cashierPid, "Cashier");
        terminateProcess(g_worker1Pid, "Worker1");
        terminateProcess(g_worker2Pid, "Worker2");

        for (pid_t pid : touristPids) {
            kill(pid, SIGTERM);
        }

        usleep(300000);
        while (waitpid(-1, nullptr, WNOHANG) > 0) {}

    } catch (const std::exception& e) {
        std::cerr << "[Main] Error: " << e.what() << std::endl;
        terminateProcess(g_cashierPid, "Cashier");
        terminateProcess(g_worker1Pid, "Worker1");
        terminateProcess(g_worker2Pid, "Worker2");
        cleanupIpc();
        return 1;
    }

    std::cout << "\n[Main] Cleaning up IPC structures..." << std::endl;
    cleanupIpc();
    std::cout << "[Main] Done." << std::endl;

    return 0;
}
