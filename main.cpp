#include <iostream>
#include <iomanip>
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
    std::cout << "=== Ropeway Simulation - Phase 7: Daily Reports Test ===" << std::endl;
    std::cout << "Testing: Gate logging and daily report generation\n" << std::endl;

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
        constexpr int TEST_STATION_CAPACITY = 10; // Higher capacity for child supervision test
        std::cout << "[Main] Station capacity set to " << TEST_STATION_CAPACITY << std::endl;
        sem.setValue(SemaphoreIndex::STATION_CAPACITY, TEST_STATION_CAPACITY);
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

        // Spawn tourists - testing child supervision
        std::vector<pid_t> touristPids;
        std::cout << "\n[Main] Spawning tourists (testing child supervision)..." << std::endl;

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

        // Test child supervision: children under 8 need adult guardian
        // Adults can supervise max 2 children
        std::vector<TouristConfig> tourists = {
            {1, 35, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY, "Adult 1 (potential guardian)"},
            {2, 6, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY, "Child 6yo (needs guardian)"},
            {3, 5, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY, "Child 5yo (needs guardian)"},
            {4, 40, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY, "Adult 2 (potential guardian)"},
            {5, 7, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY, "Child 7yo (needs guardian)"},
            {6, 30, TouristType::CYCLIST, false, true, -1, TrailDifficulty::MEDIUM, "Adult cyclist"},
            {7, 10, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY, "Child 10yo (no guardian needed)"},
            {8, 25, TouristType::PEDESTRIAN, false, true, -1, TrailDifficulty::EASY, "Adult 3"},
        };

        for (const auto& t : tourists) {
            std::cout << "[Main] Spawning Tourist " << t.id << ": " << t.description << std::endl;
            pid_t pid = spawnTourist(t.id, t.age, t.type, t.isVip, t.wantsToRide, t.guardianId, t.trail);
            if (pid > 0) {
                touristPids.push_back(pid);
            }
            usleep(100000); // Stagger spawns - slightly faster to test VIP priority
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

        // Generate comprehensive daily report
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "           DAILY REPORT - ROPEWAY SIMULATION" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
        {
            SemaphoreLock lock(sem, SemaphoreIndex::SHARED_MEMORY);

            // Overall statistics
            std::cout << "\n--- OVERALL STATISTICS ---" << std::endl;
            std::cout << "Final State: ";
            switch (shm->state) {
                case RopewayState::STOPPED: std::cout << "STOPPED"; break;
                case RopewayState::RUNNING: std::cout << "RUNNING"; break;
                case RopewayState::EMERGENCY_STOP: std::cout << "EMERGENCY_STOP"; break;
                case RopewayState::CLOSING: std::cout << "CLOSING"; break;
            }
            std::cout << std::endl;

            const DailyStatistics& stats = shm->dailyStats;
            std::cout << "Total Tourists Served: " << stats.totalTourists << std::endl;
            std::cout << "  - VIP Tourists: " << stats.vipTourists << std::endl;
            std::cout << "  - Children (under 10): " << stats.childrenServed << std::endl;
            std::cout << "  - Seniors (65+): " << stats.seniorsServed << std::endl;
            std::cout << "Total Rides Completed: " << stats.totalRides << std::endl;
            std::cout << "  - Cyclist Rides: " << stats.cyclistRides << std::endl;
            std::cout << "  - Pedestrian Rides: " << stats.pedestrianRides << std::endl;
            std::cout << "Emergency Stops: " << stats.emergencyStops << std::endl;

            // Per-tourist ride report
            std::cout << "\n--- RIDES PER TOURIST/TICKET ---" << std::endl;
            std::cout << std::left << std::setw(10) << "Tourist"
                      << std::setw(10) << "Ticket"
                      << std::setw(8) << "Age"
                      << std::setw(12) << "Type"
                      << std::setw(6) << "VIP"
                      << std::setw(8) << "Rides"
                      << std::setw(12) << "EntryGates"
                      << "RideGates" << std::endl;
            std::cout << std::string(76, '-') << std::endl;

            for (uint32_t i = 0; i < shm->touristRecordCount; ++i) {
                const TouristRideRecord& rec = shm->touristRecords[i];
                std::cout << std::left << std::setw(10) << rec.touristId
                          << std::setw(10) << rec.ticketId
                          << std::setw(8) << rec.age
                          << std::setw(12) << (rec.type == TouristType::CYCLIST ? "CYCLIST" : "PEDESTRIAN")
                          << std::setw(6) << (rec.isVip ? "Yes" : "No")
                          << std::setw(8) << rec.ridesCompleted
                          << std::setw(12) << rec.entryGatePassages
                          << rec.rideGatePassages << std::endl;
            }

            // Gate passage log summary
            std::cout << "\n--- GATE PASSAGE LOG ---" << std::endl;
            std::cout << "Total Gate Passages Recorded: " << shm->gateLog.count << std::endl;

            uint32_t entryAllowed = 0, entryDenied = 0, rideAllowed = 0, rideDenied = 0;
            for (uint32_t i = 0; i < shm->gateLog.count; ++i) {
                const GatePassage& p = shm->gateLog.entries[i];
                if (p.gateType == GateType::ENTRY) {
                    if (p.wasAllowed) entryAllowed++; else entryDenied++;
                } else {
                    if (p.wasAllowed) rideAllowed++; else rideDenied++;
                }
            }
            std::cout << "Entry Gates: " << entryAllowed << " allowed, " << entryDenied << " denied" << std::endl;
            std::cout << "Ride Gates:  " << rideAllowed << " allowed, " << rideDenied << " denied" << std::endl;

            // Recent gate passages
            if (shm->gateLog.count > 0) {
                std::cout << "\nRecent Gate Passages:" << std::endl;
                uint32_t start = (shm->gateLog.count > 10) ? shm->gateLog.count - 10 : 0;
                for (uint32_t i = start; i < shm->gateLog.count; ++i) {
                    const GatePassage& p = shm->gateLog.entries[i];
                    char timeStr[32];
                    struct tm* tm_info = localtime(&p.timestamp);
                    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", tm_info);
                    std::cout << "  [" << timeStr << "] "
                              << (p.gateType == GateType::ENTRY ? "ENTRY" : "RIDE ")
                              << " Gate " << p.gateNumber
                              << " - Tourist " << p.touristId
                              << " (Ticket " << p.ticketId << ")"
                              << " - " << (p.wasAllowed ? "ALLOWED" : "DENIED") << std::endl;
                }
            }
        }
        std::cout << std::string(60, '=') << std::endl;

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
