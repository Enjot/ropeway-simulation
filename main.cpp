#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
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
    std::cout << "=== Ropeway Simulation - Phase 9: Emergency Stop/Resume Test ===" << std::endl;
    std::cout << "Testing: Emergency stop trigger, worker communication, and resume protocol\n" << std::endl;

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
        time_t simulationStartTime = time(nullptr);
        shm->state = RopewayState::RUNNING;
        shm->acceptingNewTourists = true;
        shm->openingTime = simulationStartTime;
        shm->closingTime = simulationStartTime + 25; // 25 seconds simulation (emergency test)
        shm->dailyStats.simulationStartTime = simulationStartTime;

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

        // Test various tourists including VIPs, children, seniors
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
            pid_t pid = spawnTourist(t.id, t.age, t.type, t.isVip, t.wantsToRide, t.guardianId, t.trail);
            if (pid > 0) {
                touristPids.push_back(pid);
            }
            usleep(100000); // Stagger spawns - slightly faster to test VIP priority
        }

        // Main simulation loop
        std::cout << "\n[Main] Simulation running..." << std::endl;
        std::cout << "[Main] Emergency stop scheduled at 8 seconds" << std::endl;
        std::cout << "[Main] Resume scheduled at 13 seconds" << std::endl;

        time_t startTime = time(nullptr);
        bool emergencyTriggered = false;
        bool resumeTriggered = false;

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

            // Trigger emergency stop at 8 seconds
            if (elapsed >= 8 && !emergencyTriggered) {
                std::cout << "\n[Main] >>> TRIGGERING EMERGENCY STOP <<<" << std::endl;
                std::cout << "[Main] Sending SIGUSR1 to Worker1 (PID: " << g_worker1Pid << ")" << std::endl;
                if (g_worker1Pid > 0) {
                    kill(g_worker1Pid, SIGUSR1);
                }
                emergencyTriggered = true;
            }

            // Trigger resume at 13 seconds
            if (elapsed >= 13 && emergencyTriggered && !resumeTriggered) {
                std::cout << "\n[Main] >>> TRIGGERING RESUME <<<" << std::endl;
                std::cout << "[Main] Sending SIGUSR2 to Worker1 (PID: " << g_worker1Pid << ")" << std::endl;
                if (g_worker1Pid > 0) {
                    kill(g_worker1Pid, SIGUSR2);
                }
                resumeTriggered = true;
            }

            // Timeout after 30 seconds
            if (elapsed >= 30) {
                std::cout << "\n[Main] Timeout reached." << std::endl;
                break;
            }

            usleep(500000); // 500ms
        }

        // Record simulation end time
        time_t simulationEndTime = time(nullptr);

        // Generate comprehensive daily report to both console and file
        std::stringstream report;
        {
            SemaphoreLock lock(sem, SemaphoreIndex::SHARED_MEMORY);
            shm->dailyStats.simulationEndTime = simulationEndTime;

            // Format timestamps
            char startTimeStr[64], endTimeStr[64];
            struct tm* tm_start = localtime(&shm->dailyStats.simulationStartTime);
            strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%d %H:%M:%S", tm_start);
            struct tm* tm_end = localtime(&shm->dailyStats.simulationEndTime);
            strftime(endTimeStr, sizeof(endTimeStr), "%Y-%m-%d %H:%M:%S", tm_end);

            report << std::string(60, '=') << "\n";
            report << "           DAILY REPORT - ROPEWAY SIMULATION\n";
            report << std::string(60, '=') << "\n\n";

            // Timing information
            report << "--- SIMULATION TIMING ---\n";
            report << "Start Time: " << startTimeStr << "\n";
            report << "End Time:   " << endTimeStr << "\n";
            report << "Duration:   " << (simulationEndTime - shm->dailyStats.simulationStartTime) << " seconds\n";

            // Overall statistics
            report << "\n--- OVERALL STATISTICS ---\n";
            report << "Final State: ";
            switch (shm->state) {
                case RopewayState::STOPPED: report << "STOPPED"; break;
                case RopewayState::RUNNING: report << "RUNNING"; break;
                case RopewayState::EMERGENCY_STOP: report << "EMERGENCY_STOP"; break;
                case RopewayState::CLOSING: report << "CLOSING"; break;
            }
            report << "\n";

            const DailyStatistics& stats = shm->dailyStats;
            report << "Total Tourists Served: " << stats.totalTourists << "\n";
            report << "  - VIP Tourists: " << stats.vipTourists << "\n";
            report << "  - Children (under 10): " << stats.childrenServed << "\n";
            report << "  - Seniors (65+): " << stats.seniorsServed << "\n";
            report << "Total Rides Completed: " << stats.totalRides << "\n";
            report << "  - Cyclist Rides: " << stats.cyclistRides << "\n";
            report << "  - Pedestrian Rides: " << stats.pedestrianRides << "\n";
            report << "Total Revenue: " << std::fixed << std::setprecision(2) << stats.totalRevenueWithDiscounts << "\n";
            report << "Emergency Stops: " << stats.emergencyStops << "\n";
            if (stats.totalEmergencyDuration > 0) {
                report << "Total Emergency Duration: " << stats.totalEmergencyDuration << " seconds\n";
            }

            // Per-tourist ride report
            report << "\n--- RIDES PER TOURIST/TICKET ---\n";
            report << std::left << std::setw(10) << "Tourist"
                   << std::setw(10) << "Ticket"
                   << std::setw(8) << "Age"
                   << std::setw(12) << "Type"
                   << std::setw(6) << "VIP"
                   << std::setw(8) << "Rides"
                   << std::setw(12) << "EntryGates"
                   << "RideGates\n";
            report << std::string(76, '-') << "\n";

            for (uint32_t i = 0; i < shm->touristRecordCount; ++i) {
                const TouristRideRecord& rec = shm->touristRecords[i];
                report << std::left << std::setw(10) << rec.touristId
                       << std::setw(10) << rec.ticketId
                       << std::setw(8) << rec.age
                       << std::setw(12) << (rec.type == TouristType::CYCLIST ? "CYCLIST" : "PEDESTRIAN")
                       << std::setw(6) << (rec.isVip ? "Yes" : "No")
                       << std::setw(8) << rec.ridesCompleted
                       << std::setw(12) << rec.entryGatePassages
                       << rec.rideGatePassages << "\n";
            }

            // Emergency stop log
            if (stats.emergencyRecordCount > 0) {
                report << "\n--- EMERGENCY STOP LOG ---\n";
                for (uint32_t i = 0; i < stats.emergencyRecordCount; ++i) {
                    const EmergencyStopRecord& rec = stats.emergencyRecords[i];
                    char startTimeStr[32], endTimeStr[32];
                    struct tm* tm_start = localtime(&rec.startTime);
                    strftime(startTimeStr, sizeof(startTimeStr), "%H:%M:%S", tm_start);

                    report << "  [" << (i + 1) << "] Started: " << startTimeStr
                           << " by Worker" << rec.initiatorWorkerId;

                    if (rec.resumed && rec.endTime > 0) {
                        struct tm* tm_end = localtime(&rec.endTime);
                        strftime(endTimeStr, sizeof(endTimeStr), "%H:%M:%S", tm_end);
                        report << " | Resumed: " << endTimeStr
                               << " | Duration: " << (rec.endTime - rec.startTime) << "s";
                    } else {
                        report << " | NOT RESUMED";
                    }
                    report << "\n";
                }
            }

            // Gate passage log summary
            report << "\n--- GATE PASSAGE LOG ---\n";
            report << "Total Gate Passages Recorded: " << shm->gateLog.count << "\n";

            uint32_t entryAllowed = 0, entryDenied = 0, rideAllowed = 0, rideDenied = 0;
            for (uint32_t i = 0; i < shm->gateLog.count; ++i) {
                const GatePassage& p = shm->gateLog.entries[i];
                if (p.gateType == GateType::ENTRY) {
                    if (p.wasAllowed) entryAllowed++; else entryDenied++;
                } else {
                    if (p.wasAllowed) rideAllowed++; else rideDenied++;
                }
            }
            report << "Entry Gates: " << entryAllowed << " allowed, " << entryDenied << " denied\n";
            report << "Ride Gates:  " << rideAllowed << " allowed, " << rideDenied << " denied\n";

            // All gate passages for file report
            report << "\n--- FULL GATE PASSAGE LOG ---\n";
            for (uint32_t i = 0; i < shm->gateLog.count; ++i) {
                const GatePassage& p = shm->gateLog.entries[i];
                char timeStr[32];
                struct tm* tm_info = localtime(&p.timestamp);
                strftime(timeStr, sizeof(timeStr), "%H:%M:%S", tm_info);
                report << "  [" << timeStr << "] "
                       << (p.gateType == GateType::ENTRY ? "ENTRY" : "RIDE ")
                       << " Gate " << p.gateNumber
                       << " - Tourist " << p.touristId
                       << " (Ticket " << p.ticketId << ")"
                       << " - " << (p.wasAllowed ? "ALLOWED" : "DENIED") << "\n";
            }

            report << std::string(60, '=') << "\n";
        }

        // Output to console (truncated gate log)
        std::cout << "\n" << report.str();

        // Save full report to file
        std::string reportFilename = "daily_report_" + std::to_string(simulationEndTime) + ".txt";
        std::ofstream reportFile(reportFilename);
        if (reportFile.is_open()) {
            reportFile << report.str();
            reportFile.close();
            std::cout << "\n[Main] Daily report saved to: " << reportFilename << std::endl;
        } else {
            std::cerr << "[Main] Warning: Could not save report to file" << std::endl;
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
