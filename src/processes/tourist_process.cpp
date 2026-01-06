#include <iostream>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>

#include "ipc/SharedMemory.hpp"
#include "ipc/Semaphore.hpp"
#include "ipc/MessageQueue.hpp"
#include "ipc/ropeway_system_state.hpp"
#include "ipc/semaphore_index.hpp"
#include "ipc/cashier_message.hpp"
#include "structures/tourist.hpp"
#include "gates/EntryGate.hpp"
#include "gates/RideGate.hpp"
#include "common/config.hpp"

namespace {
    volatile sig_atomic_t g_emergencyStop = 0;
    volatile sig_atomic_t g_shouldExit = 0;

    void signalHandler(int signum) {
        if (signum == SIGUSR1) {
            g_emergencyStop = 1;
        } else if (signum == SIGTERM || signum == SIGINT) {
            g_shouldExit = 1;
        }
    }

    void setupSignalHandlers() {
        struct sigaction sa{};
        sa.sa_handler = signalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        if (sigaction(SIGUSR1, &sa, nullptr) == -1) {
            perror("sigaction SIGUSR1");
        }
        if (sigaction(SIGTERM, &sa, nullptr) == -1) {
            perror("sigaction SIGTERM");
        }
        if (sigaction(SIGINT, &sa, nullptr) == -1) {
            perror("sigaction SIGINT");
        }
    }

    void printUsage(const char* programName) {
        std::cerr << "Usage: " << programName
                  << " <id> <age> <type> <isVip> <wantsToRide> <guardianId> <trail> <shmKey> <semKey> <msgKey> <cashierMsgKey>\n"
                  << "  id:            Tourist ID (uint32)\n"
                  << "  age:           Tourist age (uint32)\n"
                  << "  type:          0=PEDESTRIAN, 1=CYCLIST\n"
                  << "  isVip:         0=no, 1=yes (request VIP)\n"
                  << "  wantsToRide:   0=no, 1=yes\n"
                  << "  guardianId:    Guardian tourist ID (-1 if none)\n"
                  << "  trail:         0=EASY, 1=MEDIUM, 2=HARD\n"
                  << "  shmKey:        Shared memory key\n"
                  << "  semKey:        Semaphore key\n"
                  << "  msgKey:        Worker message queue key\n"
                  << "  cashierMsgKey: Cashier message queue key\n";
    }

    struct TouristArgs {
        uint32_t id;
        uint32_t age;
        TouristType type;
        bool isVip;
        bool wantsToRide;
        int32_t guardianId;
        TrailDifficulty trail;
        key_t shmKey;
        key_t semKey;
        key_t msgKey;
        key_t cashierMsgKey;
    };

    bool parseArgs(int argc, char* argv[], TouristArgs& args) {
        if (argc != 12) {
            printUsage(argv[0]);
            return false;
        }

        char* endPtr = nullptr;

        args.id = static_cast<uint32_t>(std::strtoul(argv[1], &endPtr, 10));
        if (*endPtr != '\0') {
            std::cerr << "Error: Invalid id\n";
            return false;
        }

        args.age = static_cast<uint32_t>(std::strtoul(argv[2], &endPtr, 10));
        if (*endPtr != '\0') {
            std::cerr << "Error: Invalid age\n";
            return false;
        }

        int typeVal = static_cast<int>(std::strtol(argv[3], &endPtr, 10));
        if (*endPtr != '\0' || typeVal < 0 || typeVal > 1) {
            std::cerr << "Error: Invalid type (must be 0 or 1)\n";
            return false;
        }
        args.type = static_cast<TouristType>(typeVal);

        int vipVal = static_cast<int>(std::strtol(argv[4], &endPtr, 10));
        if (*endPtr != '\0' || vipVal < 0 || vipVal > 1) {
            std::cerr << "Error: Invalid isVip (must be 0 or 1)\n";
            return false;
        }
        args.isVip = (vipVal == 1);

        int rideVal = static_cast<int>(std::strtol(argv[5], &endPtr, 10));
        if (*endPtr != '\0' || rideVal < 0 || rideVal > 1) {
            std::cerr << "Error: Invalid wantsToRide (must be 0 or 1)\n";
            return false;
        }
        args.wantsToRide = (rideVal == 1);

        args.guardianId = static_cast<int32_t>(std::strtol(argv[6], &endPtr, 10));
        if (*endPtr != '\0') {
            std::cerr << "Error: Invalid guardianId\n";
            return false;
        }

        int trailVal = static_cast<int>(std::strtol(argv[7], &endPtr, 10));
        if (*endPtr != '\0' || trailVal < 0 || trailVal > 2) {
            std::cerr << "Error: Invalid trail (must be 0, 1, or 2)\n";
            return false;
        }
        args.trail = static_cast<TrailDifficulty>(trailVal);

        args.shmKey = static_cast<key_t>(std::strtol(argv[8], &endPtr, 10));
        if (*endPtr != '\0') {
            std::cerr << "Error: Invalid shmKey\n";
            return false;
        }

        args.semKey = static_cast<key_t>(std::strtol(argv[9], &endPtr, 10));
        if (*endPtr != '\0') {
            std::cerr << "Error: Invalid semKey\n";
            return false;
        }

        args.msgKey = static_cast<key_t>(std::strtol(argv[10], &endPtr, 10));
        if (*endPtr != '\0') {
            std::cerr << "Error: Invalid msgKey\n";
            return false;
        }

        args.cashierMsgKey = static_cast<key_t>(std::strtol(argv[11], &endPtr, 10));
        if (*endPtr != '\0') {
            std::cerr << "Error: Invalid cashierMsgKey\n";
            return false;
        }

        return true;
    }

    const char* stateToString(TouristState state) {
        switch (state) {
            case TouristState::ARRIVING: return "ARRIVING";
            case TouristState::AT_CASHIER: return "AT_CASHIER";
            case TouristState::WAITING_ENTRY: return "WAITING_ENTRY";
            case TouristState::ON_STATION: return "ON_STATION";
            case TouristState::WAITING_PLATFORM: return "WAITING_PLATFORM";
            case TouristState::ON_CHAIR: return "ON_CHAIR";
            case TouristState::AT_TOP: return "AT_TOP";
            case TouristState::ON_TRAIL: return "ON_TRAIL";
            case TouristState::LEAVING: return "LEAVING";
            case TouristState::FINISHED: return "FINISHED";
            default: return "UNKNOWN";
        }
    }

    const char* typeToString(TouristType type) {
        return (type == TouristType::CYCLIST) ? "CYCLIST" : "PEDESTRIAN";
    }

    uint32_t getTrailTime(TrailDifficulty trail) {
        switch (trail) {
            case TrailDifficulty::EASY: return Config::Trail::TRAIL_TIME_EASY_S;
            case TrailDifficulty::MEDIUM: return Config::Trail::TRAIL_TIME_MEDIUM_S;
            case TrailDifficulty::HARD: return Config::Trail::TRAIL_TIME_HARD_S;
            default: return Config::Trail::TRAIL_TIME_EASY_S;
        }
    }
}

class TouristProcess {
public:
    TouristProcess(const TouristArgs& args)
        : tourist_{},
          shm_{args.shmKey, false},
          sem_{args.semKey, SemaphoreIndex::TOTAL_SEMAPHORES, false},
          cashierRequestQueue_{args.cashierMsgKey, false},
          cashierResponseQueue_{args.cashierMsgKey, false},
          entryGate_{sem_, shm_.get()},
          rideGate_{sem_, shm_.get()},
          requestVip_{args.isVip},
          assignedChairId_{-1} {

        tourist_.id = args.id;
        tourist_.pid = getpid();
        tourist_.age = args.age;
        tourist_.type = args.type;
        tourist_.isVip = false; // Will be set by cashier
        tourist_.wantsToRide = args.wantsToRide;
        tourist_.guardianId = args.guardianId;
        tourist_.preferredTrail = args.trail;
        tourist_.state = TouristState::ARRIVING;
        tourist_.arrivalTime = time(nullptr);

        std::cout << "[Tourist " << tourist_.id << "] Started: "
                  << typeToString(tourist_.type)
                  << ", age=" << tourist_.age
                  << ", requestVIP=" << (requestVip_ ? "yes" : "no")
                  << ", wantsToRide=" << (tourist_.wantsToRide ? "yes" : "no")
                  << std::endl;
    }

    void run() {
        while (tourist_.state != TouristState::FINISHED && !g_shouldExit) {
            if (g_emergencyStop) {
                handleEmergencyStop();
            }

            switch (tourist_.state) {
                case TouristState::ARRIVING:
                    handleArriving();
                    break;
                case TouristState::AT_CASHIER:
                    handleAtCashier();
                    break;
                case TouristState::WAITING_ENTRY:
                    handleWaitingEntry();
                    break;
                case TouristState::ON_STATION:
                    handleOnStation();
                    break;
                case TouristState::WAITING_PLATFORM:
                    handleWaitingPlatform();
                    break;
                case TouristState::ON_CHAIR:
                    handleOnChair();
                    break;
                case TouristState::AT_TOP:
                    handleAtTop();
                    break;
                case TouristState::ON_TRAIL:
                    handleOnTrail();
                    break;
                case TouristState::LEAVING:
                    handleLeaving();
                    break;
                default:
                    break;
            }
        }

        std::cout << "[Tourist " << tourist_.id << "] Finished, rides completed: "
                  << tourist_.ridesCompleted << std::endl;
    }

private:
    void changeState(TouristState newState) {
        std::cout << "[Tourist " << tourist_.id << "] "
                  << stateToString(tourist_.state) << " -> "
                  << stateToString(newState) << std::endl;
        tourist_.state = newState;
    }

    void handleEmergencyStop() {
        std::cout << "[Tourist " << tourist_.id << "] Emergency stop detected, waiting..." << std::endl;
        while (g_emergencyStop && !g_shouldExit) {
            usleep(100000); // 100ms
        }
        std::cout << "[Tourist " << tourist_.id << "] Emergency cleared, resuming" << std::endl;
    }

    void handleArriving() {
        // Simulate arrival time
        usleep(100000 + (rand() % 200000)); // 100-300ms

        // Check if ropeway is accepting tourists
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            if (!shm_->acceptingNewTourists) {
                std::cout << "[Tourist " << tourist_.id << "] Ropeway not accepting tourists, leaving" << std::endl;
                changeState(TouristState::LEAVING);
                return;
            }
        }

        changeState(TouristState::AT_CASHIER);
    }

    void handleAtCashier() {
        // Send ticket request to Cashier
        TicketRequest request;
        request.mtype = CashierMsgType::REQUEST;
        request.touristId = tourist_.id;
        request.touristAge = tourist_.age;
        request.requestedType = (tourist_.type == TouristType::CYCLIST)
            ? TicketType::DAILY  // Cyclists typically want multiple rides
            : TicketType::SINGLE_USE;
        request.requestVip = requestVip_;

        std::cout << "[Tourist " << tourist_.id << "] Requesting ticket from cashier..." << std::endl;

        if (!cashierRequestQueue_.send(request)) {
            std::cerr << "[Tourist " << tourist_.id << "] Failed to send ticket request" << std::endl;
            changeState(TouristState::LEAVING);
            return;
        }

        // Wait for response (with timeout)
        long myResponseType = CashierMsgType::RESPONSE_BASE + tourist_.id;
        time_t startTime = time(nullptr);
        constexpr int TIMEOUT_S = 10;

        while (time(nullptr) - startTime < TIMEOUT_S && !g_shouldExit) {
            auto response = cashierResponseQueue_.tryReceive(myResponseType);
            if (response) {
                if (response->success) {
                    tourist_.ticketId = response->ticketId;
                    tourist_.hasTicket = true;
                    tourist_.isVip = response->isVip;

                    std::cout << "[Tourist " << tourist_.id << "] Got ticket #" << response->ticketId
                              << " - Price: " << response->price;
                    if (response->discount > 0) {
                        std::cout << " (discount: " << (response->discount * 100) << "%)";
                    }
                    if (response->isVip) {
                        std::cout << " [VIP]";
                    }
                    std::cout << std::endl;

                    if (!tourist_.wantsToRide) {
                        changeState(TouristState::LEAVING);
                    } else {
                        changeState(TouristState::WAITING_ENTRY);
                    }
                } else {
                    std::cout << "[Tourist " << tourist_.id << "] Ticket denied: " << response->message << std::endl;
                    changeState(TouristState::LEAVING);
                }
                return;
            }
            usleep(50000); // 50ms
        }

        std::cerr << "[Tourist " << tourist_.id << "] Timeout waiting for ticket" << std::endl;
        changeState(TouristState::LEAVING);
    }

    void handleWaitingEntry() {
        // Validate ticket first
        auto validation = entryGate_.validateTicket(
            tourist_.ticketId,
            TicketType::DAILY, // Simplified - real impl would track ticket type
            time(nullptr) + 3600, // Valid for 1 hour
            tourist_.isVip
        );

        if (!validation.allowed) {
            std::cout << "[Tourist " << tourist_.id << "] Entry denied: " << validation.reason << std::endl;
            changeState(TouristState::LEAVING);
            return;
        }

        // Try to enter through gate (VIP gets priority)
        uint32_t gateNumber = 0;
        std::cout << "[Tourist " << tourist_.id << "] Waiting for entry gate"
                  << (tourist_.isVip ? " [VIP PRIORITY]" : "") << "..." << std::endl;

        if (!entryGate_.tryEnter(tourist_.id, tourist_.isVip, gateNumber)) {
            std::cerr << "[Tourist " << tourist_.id << "] Failed to enter station" << std::endl;
            changeState(TouristState::LEAVING);
            return;
        }

        // Update shared state
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            shm_->touristsInLowerStation++;
        }

        std::cout << "[Tourist " << tourist_.id << "] Entered through entry gate " << gateNumber
                  << (tourist_.isVip ? " [VIP]" : "") << std::endl;
        changeState(TouristState::ON_STATION);
    }

    void handleOnStation() {
        // Simulate being on station, then proceed to platform
        usleep(100000 + (rand() % 200000)); // 100-300ms

        // Add to boarding queue
        BoardingQueueEntry entry;
        entry.touristId = tourist_.id;
        entry.touristPid = tourist_.pid;
        entry.age = tourist_.age;
        entry.type = tourist_.type;
        entry.guardianId = tourist_.guardianId;
        entry.needsSupervision = tourist_.needsSupervision();
        entry.isAdult = tourist_.isAdult();
        entry.dependentCount = 0;
        entry.assignedChairId = -1;
        entry.readyToBoard = false;

        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            if (!shm_->boardingQueue.addTourist(entry)) {
                std::cerr << "[Tourist " << tourist_.id << "] Boarding queue full!" << std::endl;
                changeState(TouristState::LEAVING);
                return;
            }
        }

        // Child without pre-assigned guardian waits for pairing
        if (tourist_.needsSupervision() && tourist_.guardianId == -1) {
            std::cout << "[Tourist " << tourist_.id << "] Child (age " << tourist_.age
                      << ") waiting for adult guardian..." << std::endl;

            // Wait for Worker1 to assign a guardian
            bool guardianAssigned = false;
            time_t waitStart = time(nullptr);
            constexpr int GUARDIAN_TIMEOUT_S = 10;

            while (!guardianAssigned && !g_shouldExit) {
                usleep(100000); // 100ms

                {
                    SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                    int32_t idx = shm_->boardingQueue.findTourist(tourist_.id);
                    if (idx >= 0 && shm_->boardingQueue.entries[idx].guardianId != -1) {
                        tourist_.guardianId = shm_->boardingQueue.entries[idx].guardianId;
                        guardianAssigned = true;
                        std::cout << "[Tourist " << tourist_.id << "] Assigned guardian: Tourist "
                                  << tourist_.guardianId << std::endl;
                    }
                }

                if (time(nullptr) - waitStart > GUARDIAN_TIMEOUT_S) {
                    std::cout << "[Tourist " << tourist_.id << "] No guardian available, leaving" << std::endl;
                    // Remove from queue
                    {
                        SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                        int32_t idx = shm_->boardingQueue.findTourist(tourist_.id);
                        if (idx >= 0) {
                            shm_->boardingQueue.removeTourist(static_cast<uint32_t>(idx));
                        }
                    }
                    changeState(TouristState::LEAVING);
                    return;
                }
            }
        }

        changeState(TouristState::WAITING_PLATFORM);
    }

    void handleWaitingPlatform() {
        std::cout << "[Tourist " << tourist_.id << "] Waiting for chair..." << std::endl;

        // Wait for Worker1 to assign us to a chair
        time_t waitStart = time(nullptr);
        constexpr int BOARDING_TIMEOUT_S = 30;

        while (!g_shouldExit) {
            usleep(100000); // 100ms

            // Check ropeway state and boarding status
            {
                SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);

                // Check for emergency stop
                if (shm_->state == RopewayState::EMERGENCY_STOP) {
                    std::cout << "[Tourist " << tourist_.id << "] Ropeway emergency stop, waiting..." << std::endl;
                    continue;
                }

                // Check for closing
                if (shm_->state == RopewayState::CLOSING || shm_->state == RopewayState::STOPPED) {
                    std::cout << "[Tourist " << tourist_.id << "] Ropeway closing, leaving" << std::endl;
                    // Remove from boarding queue
                    int32_t idx = shm_->boardingQueue.findTourist(tourist_.id);
                    if (idx >= 0) {
                        shm_->boardingQueue.removeTourist(static_cast<uint32_t>(idx));
                    }
                    if (shm_->touristsInLowerStation > 0) {
                        shm_->touristsInLowerStation--;
                    }
                    changeState(TouristState::LEAVING);
                    return;
                }

                // Check if Worker1 has assigned us to a chair
                int32_t idx = shm_->boardingQueue.findTourist(tourist_.id);
                if (idx >= 0) {
                    BoardingQueueEntry& entry = shm_->boardingQueue.entries[idx];
                    if (entry.readyToBoard && entry.assignedChairId >= 0) {
                        assignedChairId_ = entry.assignedChairId;
                        std::cout << "[Tourist " << tourist_.id << "] Assigned to chair "
                                  << assignedChairId_ << std::endl;

                        // Remove from queue
                        shm_->boardingQueue.removeTourist(static_cast<uint32_t>(idx));

                        // Update counts
                        if (shm_->touristsInLowerStation > 0) {
                            shm_->touristsInLowerStation--;
                        }
                        shm_->touristsOnPlatform++;

                        changeState(TouristState::ON_CHAIR);
                        return;
                    }
                } else {
                    // Not in queue anymore - something went wrong
                    std::cerr << "[Tourist " << tourist_.id << "] Lost from boarding queue!" << std::endl;
                    changeState(TouristState::LEAVING);
                    return;
                }
            }

            // Timeout check
            if (time(nullptr) - waitStart > BOARDING_TIMEOUT_S) {
                std::cout << "[Tourist " << tourist_.id << "] Boarding timeout, leaving" << std::endl;
                {
                    SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                    int32_t idx = shm_->boardingQueue.findTourist(tourist_.id);
                    if (idx >= 0) {
                        shm_->boardingQueue.removeTourist(static_cast<uint32_t>(idx));
                    }
                    if (shm_->touristsInLowerStation > 0) {
                        shm_->touristsInLowerStation--;
                    }
                }
                changeState(TouristState::LEAVING);
                return;
            }
        }
    }

    void handleOnChair() {
        std::cout << "[Tourist " << tourist_.id << "] On chair " << assignedChairId_
                  << ", riding up..." << std::endl;

        // Simulate ride time (scaled down for testing)
        uint32_t rideTime = Config::Chair::RIDE_TIME_S;
        // Scale down: 300s -> 3s for testing
        usleep((rideTime / 100) * 1000000);

        // Update counts and release chair
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            if (shm_->touristsOnPlatform > 0) {
                shm_->touristsOnPlatform--;
            }
            shm_->totalRidesToday++;

            // Release chair (only first passenger does this to avoid double-release)
            if (assignedChairId_ >= 0 && static_cast<uint32_t>(assignedChairId_) < Config::Chair::QUANTITY) {
                Chair& chair = shm_->chairs[assignedChairId_];
                // Check if we're the first passenger (responsible for releasing)
                if (chair.passengerIds[0] == static_cast<int32_t>(tourist_.id)) {
                    chair.isOccupied = false;
                    chair.numPassengers = 0;
                    chair.slotsUsed = 0;
                    for (int i = 0; i < 4; ++i) {
                        chair.passengerIds[i] = -1;
                    }
                    if (shm_->chairsInUse > 0) {
                        shm_->chairsInUse--;
                    }
                    std::cout << "[Tourist " << tourist_.id << "] Released chair " << assignedChairId_ << std::endl;
                }
            }
        }

        tourist_.ridesCompleted++;
        tourist_.lastRideTime = time(nullptr);
        assignedChairId_ = -1; // Clear assigned chair

        // Signal station capacity is freed
        sem_.signal(SemaphoreIndex::STATION_CAPACITY);

        changeState(TouristState::AT_TOP);
    }

    void handleAtTop() {
        std::cout << "[Tourist " << tourist_.id << "] Arrived at top station" << std::endl;

        // Simulate exit time through one of 2 exit routes
        usleep(100000 + (rand() % 200000)); // 100-300ms

        if (tourist_.type == TouristType::CYCLIST) {
            changeState(TouristState::ON_TRAIL);
        } else {
            // Pedestrians leave after one ride
            changeState(TouristState::LEAVING);
        }
    }

    void handleOnTrail() {
        uint32_t trailTime = getTrailTime(tourist_.preferredTrail);
        std::cout << "[Tourist " << tourist_.id << "] Cycling down trail (difficulty: "
                  << static_cast<int>(tourist_.preferredTrail) << ")" << std::endl;

        // Scale down: 180-420s -> 1.8-4.2s for testing
        usleep((trailTime / 100) * 1000000);

        std::cout << "[Tourist " << tourist_.id << "] Finished trail descent" << std::endl;

        // Check if want another ride (simplified - always want more rides until ropeway closes)
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            if (shm_->acceptingNewTourists && shm_->state == RopewayState::RUNNING) {
                changeState(TouristState::WAITING_ENTRY);
                return;
            }
        }

        changeState(TouristState::LEAVING);
    }

    void handleLeaving() {
        std::cout << "[Tourist " << tourist_.id << "] Leaving the area" << std::endl;
        usleep(100000); // 100ms
        changeState(TouristState::FINISHED);
    }

    Tourist tourist_;
    SharedMemory<RopewaySystemState> shm_;
    Semaphore sem_;
    MessageQueue<TicketRequest> cashierRequestQueue_;
    MessageQueue<TicketResponse> cashierResponseQueue_;
    EntryGate entryGate_;
    RideGate rideGate_;
    bool requestVip_;
    int32_t assignedChairId_;
};

int main(int argc, char* argv[]) {
    TouristArgs args{};
    if (!parseArgs(argc, argv, args)) {
        return 1;
    }

    setupSignalHandlers();
    srand(static_cast<unsigned>(time(nullptr)) ^ static_cast<unsigned>(getpid()));

    try {
        TouristProcess process(args);
        process.run();
    } catch (const std::exception& e) {
        std::cerr << "[Tourist " << args.id << "] Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
