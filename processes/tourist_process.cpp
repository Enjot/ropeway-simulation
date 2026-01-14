#include <iostream>
#include <unistd.h>
#include <ctime>
#include <cstdlib>

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
#include "utils/SignalHelper.hpp"
#include "utils/EnumStrings.hpp"
#include "utils/ArgumentParser.hpp"

namespace {
    SignalHelper::SignalFlags g_signals;
}

class TouristProcess {
public:
    TouristProcess(const ArgumentParser::TouristArgs& args)
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
        tourist_.type = static_cast<TouristType>(args.type);
        tourist_.isVip = false;
        tourist_.wantsToRide = args.wantsToRide;
        tourist_.guardianId = args.guardianId;
        tourist_.preferredTrail = static_cast<TrailDifficulty>(args.trail);
        tourist_.state = TouristState::ARRIVING;
        tourist_.arrivalTime = time(nullptr);

        std::cout << "[Tourist " << tourist_.id << "] Started: "
                  << EnumStrings::toString(tourist_.type)
                  << ", age=" << tourist_.age
                  << ", requestVIP=" << (requestVip_ ? "yes" : "no")
                  << ", wantsToRide=" << (tourist_.wantsToRide ? "yes" : "no")
                  << std::endl;
    }

    void run() {
        while (tourist_.state != TouristState::FINISHED && !SignalHelper::shouldExit(g_signals)) {
            if (SignalHelper::isEmergency(g_signals)) {
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
                  << EnumStrings::toString(tourist_.state) << " -> "
                  << EnumStrings::toString(newState) << std::endl;
        tourist_.state = newState;
    }

    void handleEmergencyStop() {
        std::cout << "[Tourist " << tourist_.id << "] Emergency stop detected, waiting..." << std::endl;
        while (SignalHelper::isEmergency(g_signals) && !SignalHelper::shouldExit(g_signals)) {
            usleep(100000);
        }
        std::cout << "[Tourist " << tourist_.id << "] Emergency cleared, resuming" << std::endl;
    }

    void handleArriving() {
        usleep(100000 + (rand() % 200000));

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
        TicketRequest request;
        request.mtype = CashierMsgType::REQUEST;
        request.touristId = tourist_.id;
        request.touristAge = tourist_.age;
        request.requestedType = (tourist_.type == TouristType::CYCLIST)
            ? TicketType::DAILY
            : TicketType::SINGLE_USE;
        request.requestVip = requestVip_;

        std::cout << "[Tourist " << tourist_.id << "] Requesting ticket from cashier..." << std::endl;

        if (!cashierRequestQueue_.send(request)) {
            std::cerr << "[Tourist " << tourist_.id << "] Failed to send ticket request" << std::endl;
            changeState(TouristState::LEAVING);
            return;
        }

        long myResponseType = CashierMsgType::RESPONSE_BASE + tourist_.id;
        time_t startTime = time(nullptr);
        constexpr int TIMEOUT_S = 10;

        while (time(nullptr) - startTime < TIMEOUT_S && !SignalHelper::shouldExit(g_signals)) {
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

                    {
                        SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                        shm_->registerTourist(tourist_.id, tourist_.ticketId,
                                              tourist_.age, tourist_.type, tourist_.isVip);
                        shm_->dailyStats.totalTourists++;
                        if (tourist_.isVip) shm_->dailyStats.vipTourists++;
                        if (tourist_.age < 10) shm_->dailyStats.childrenServed++;
                        if (tourist_.age >= 65) shm_->dailyStats.seniorsServed++;
                    }

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
            usleep(50000);
        }

        std::cerr << "[Tourist " << tourist_.id << "] Timeout waiting for ticket" << std::endl;
        changeState(TouristState::LEAVING);
    }

    void handleWaitingEntry() {
        auto validation = entryGate_.validateTicket(
            tourist_.ticketId,
            TicketType::DAILY,
            time(nullptr) + 3600,
            tourist_.isVip
        );

        if (!validation.allowed) {
            std::cout << "[Tourist " << tourist_.id << "] Entry denied: " << validation.reason << std::endl;
            changeState(TouristState::LEAVING);
            return;
        }

        uint32_t gateNumber = 0;
        std::cout << "[Tourist " << tourist_.id << "] Waiting for entry gate"
                  << (tourist_.isVip ? " [VIP PRIORITY]" : "") << "..." << std::endl;

        if (!entryGate_.tryEnter(tourist_.id, tourist_.isVip, gateNumber)) {
            std::cerr << "[Tourist " << tourist_.id << "] Failed to enter station" << std::endl;
            {
                SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                shm_->logGatePassage(tourist_.id, tourist_.ticketId, GateType::ENTRY, gateNumber, false);
            }
            changeState(TouristState::LEAVING);
            return;
        }

        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            shm_->touristsInLowerStation++;
            shm_->logGatePassage(tourist_.id, tourist_.ticketId, GateType::ENTRY, gateNumber, true);
        }

        std::cout << "[Tourist " << tourist_.id << "] Entered through entry gate " << gateNumber
                  << (tourist_.isVip ? " [VIP]" : "") << std::endl;
        changeState(TouristState::ON_STATION);
    }

    void handleOnStation() {
        usleep(100000 + (rand() % 200000));

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

        if (tourist_.needsSupervision() && tourist_.guardianId == -1) {
            std::cout << "[Tourist " << tourist_.id << "] Child (age " << tourist_.age
                      << ") waiting for adult guardian..." << std::endl;

            bool guardianAssigned = false;
            time_t waitStart = time(nullptr);
            constexpr int GUARDIAN_TIMEOUT_S = 10;

            while (!guardianAssigned && !SignalHelper::shouldExit(g_signals)) {
                usleep(100000);

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

        time_t waitStart = time(nullptr);
        constexpr int BOARDING_TIMEOUT_S = 30;

        while (!SignalHelper::shouldExit(g_signals)) {
            usleep(100000);

            {
                SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);

                if (shm_->state == RopewayState::EMERGENCY_STOP) {
                    std::cout << "[Tourist " << tourist_.id << "] Ropeway emergency stop, waiting..." << std::endl;
                    continue;
                }

                if (shm_->state == RopewayState::CLOSING || shm_->state == RopewayState::STOPPED) {
                    std::cout << "[Tourist " << tourist_.id << "] Ropeway closing, leaving" << std::endl;
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

                int32_t idx = shm_->boardingQueue.findTourist(tourist_.id);
                if (idx >= 0) {
                    BoardingQueueEntry& entry = shm_->boardingQueue.entries[idx];
                    if (entry.readyToBoard && entry.assignedChairId >= 0) {
                        assignedChairId_ = entry.assignedChairId;
                        std::cout << "[Tourist " << tourist_.id << "] Assigned to chair "
                                  << assignedChairId_ << std::endl;

                        shm_->boardingQueue.removeTourist(static_cast<uint32_t>(idx));

                        if (shm_->touristsInLowerStation > 0) {
                            shm_->touristsInLowerStation--;
                        }
                        shm_->touristsOnPlatform++;

                        changeState(TouristState::ON_CHAIR);
                        return;
                    }
                } else {
                    std::cerr << "[Tourist " << tourist_.id << "] Lost from boarding queue!" << std::endl;
                    changeState(TouristState::LEAVING);
                    return;
                }
            }

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

        uint32_t rideTime = Config::Chair::RIDE_TIME_S;
        usleep((rideTime / 100) * 1000000);

        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            if (shm_->touristsOnPlatform > 0) {
                shm_->touristsOnPlatform--;
            }
            shm_->totalRidesToday++;

            shm_->recordRide(tourist_.id);
            shm_->dailyStats.totalRides++;
            if (tourist_.type == TouristType::CYCLIST) {
                shm_->dailyStats.cyclistRides++;
            } else {
                shm_->dailyStats.pedestrianRides++;
            }

            uint32_t rideGateNumber = tourist_.id % Config::Gate::NUM_RIDE_GATES;
            shm_->logGatePassage(tourist_.id, tourist_.ticketId, GateType::RIDE, rideGateNumber, true);

            if (assignedChairId_ >= 0 && static_cast<uint32_t>(assignedChairId_) < Config::Chair::QUANTITY) {
                Chair& chair = shm_->chairs[assignedChairId_];
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
        assignedChairId_ = -1;

        sem_.signal(SemaphoreIndex::STATION_CAPACITY);

        changeState(TouristState::AT_TOP);
    }

    void handleAtTop() {
        std::cout << "[Tourist " << tourist_.id << "] Arrived at top station" << std::endl;

        usleep(100000 + (rand() % 200000));

        if (tourist_.type == TouristType::CYCLIST) {
            changeState(TouristState::ON_TRAIL);
        } else {
            changeState(TouristState::LEAVING);
        }
    }

    void handleOnTrail() {
        uint32_t trailTime = EnumStrings::getTrailTimeSeconds(tourist_.preferredTrail);
        std::cout << "[Tourist " << tourist_.id << "] Cycling down trail (difficulty: "
                  << static_cast<int>(tourist_.preferredTrail) << ")" << std::endl;

        usleep((trailTime / 100) * 1000000);

        std::cout << "[Tourist " << tourist_.id << "] Finished trail descent" << std::endl;

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
        usleep(100000);
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
    ArgumentParser::TouristArgs args{};
    if (!ArgumentParser::parseTouristArgs(argc, argv, args)) {
        return 1;
    }

    SignalHelper::setup(g_signals, SignalHelper::Mode::TOURIST);
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
