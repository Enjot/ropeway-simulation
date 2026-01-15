#include <unistd.h>
#include <ctime>
#include <cstdlib>
#include <cstring>

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
#include "utils/Logger.hpp"

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
        tourist_.state = TouristState::BUYING_TICKET;
        tourist_.arrivalTime = time(nullptr);

        // Build tag using async-signal-safe method
        strcpy(tag_, "Tourist ");
        char idBuf[16];
        char* ptr = idBuf + sizeof(idBuf) - 1;
        *ptr = '\0';
        unsigned int id = tourist_.id;
        do { --ptr; *ptr = '0' + (id % 10); id /= 10; } while (id > 0);
        strcat(tag_, ptr);

        Logger::info(tag_, "Started: ", EnumStrings::toString(tourist_.type),
                    requestVip_ ? ", VIP" : ", regular");
    }

    void run() {
        while (tourist_.state != TouristState::FINISHED && !SignalHelper::shouldExit(g_signals)) {
            if (SignalHelper::isEmergency(g_signals)) {
                handleEmergencyStop();
            }

            switch (tourist_.state) {
                case TouristState::BUYING_TICKET:
                    handleBuyingTicket();
                    break;
                case TouristState::WAITING_ENTRY:
                    handleWaitingEntry();
                    break;
                case TouristState::WAITING_BOARDING:
                    handleWaitingBoarding();
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
                default:
                    break;
            }
        }

        Logger::info(tag_, "Finished, rides completed: ", tourist_.ridesCompleted);
    }

private:
    void changeState(TouristState newState) {
        Logger::stateChange(tag_, EnumStrings::toString(tourist_.state),
                           EnumStrings::toString(newState));
        tourist_.state = newState;
    }

    void handleEmergencyStop() {
        Logger::info(tag_, "Emergency stop detected, waiting...");
        while (SignalHelper::isEmergency(g_signals) && !SignalHelper::shouldExit(g_signals)) {
            usleep(100000);
        }
        Logger::info(tag_, "Emergency cleared, resuming");
    }

    void handleBuyingTicket() {
        // Simulate arrival time
        usleep(100000 + (rand() % 200000));

        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            if (!shm_->acceptingNewTourists) {
                Logger::info(tag_, "Ropeway not accepting tourists, leaving");
                changeState(TouristState::FINISHED);
                return;
            }
        }

        // Request ticket from cashier
        TicketRequest request;
        request.mtype = CashierMsgType::REQUEST;
        request.touristId = tourist_.id;
        request.touristAge = tourist_.age;
        request.requestedType = (tourist_.type == TouristType::CYCLIST)
            ? TicketType::DAILY
            : TicketType::SINGLE_USE;
        request.requestVip = requestVip_;

        Logger::info(tag_, "Requesting ticket from cashier...");

        if (!cashierRequestQueue_.send(request)) {
            Logger::perr(tag_, "Failed to send ticket request");
            changeState(TouristState::FINISHED);
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

                    if (response->isVip) {
                        Logger::info(tag_, "Got ticket #", response->ticketId, " [VIP]");
                    } else {
                        Logger::info(tag_, "Got ticket #", response->ticketId);
                    }

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
                        changeState(TouristState::FINISHED);
                    } else {
                        changeState(TouristState::WAITING_ENTRY);
                    }
                } else {
                    Logger::info(tag_, "Ticket denied");
                    changeState(TouristState::FINISHED);
                }
                return;
            }
            usleep(50000);
        }

        Logger::perr(tag_, "Timeout waiting for ticket");
        changeState(TouristState::FINISHED);
    }

    void handleWaitingEntry() {
        auto validation = entryGate_.validateTicket(
            tourist_.ticketId,
            TicketType::DAILY,
            time(nullptr) + 3600,
            tourist_.isVip
        );

        if (!validation.allowed) {
            Logger::info(tag_, "Entry denied");
            changeState(TouristState::FINISHED);
            return;
        }

        uint32_t gateNumber = 0;
        if (tourist_.isVip) {
            Logger::info(tag_, "Waiting for entry gate [VIP PRIORITY]...");
        } else {
            Logger::info(tag_, "Waiting for entry gate...");
        }

        if (!entryGate_.tryEnter(tourist_.id, tourist_.isVip, gateNumber)) {
            Logger::perr(tag_, "Failed to enter station");
            {
                SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                shm_->logGatePassage(tourist_.id, tourist_.ticketId, GateType::ENTRY, gateNumber, false);
            }
            changeState(TouristState::FINISHED);
            return;
        }

        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            shm_->touristsInLowerStation++;
            shm_->logGatePassage(tourist_.id, tourist_.ticketId, GateType::ENTRY, gateNumber, true);
        }

        if (tourist_.isVip) {
            Logger::info(tag_, "Entered through entry gate ", gateNumber, " [VIP]");
        } else {
            Logger::info(tag_, "Entered through entry gate ", gateNumber);
        }
        changeState(TouristState::WAITING_BOARDING);
    }

    void handleWaitingBoarding() {
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
                Logger::perr(tag_, "Boarding queue full!");
                changeState(TouristState::FINISHED);
                return;
            }
        }

        // Handle child-guardian pairing for children under 8
        if (tourist_.needsSupervision() && tourist_.guardianId == -1) {
            Logger::info(tag_, "Child (age ", tourist_.age, ") waiting for adult guardian...");

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
                        Logger::info(tag_, "Assigned guardian: Tourist ", tourist_.guardianId);
                    }
                }

                if (time(nullptr) - waitStart > GUARDIAN_TIMEOUT_S) {
                    Logger::info(tag_, "No guardian available, leaving");
                    {
                        SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                        int32_t idx = shm_->boardingQueue.findTourist(tourist_.id);
                        if (idx >= 0) {
                            shm_->boardingQueue.removeTourist(static_cast<uint32_t>(idx));
                        }
                    }
                    changeState(TouristState::FINISHED);
                    return;
                }
            }
        }

        // Wait for chair assignment
        Logger::info(tag_, "Waiting for chair...");

        time_t waitStart = time(nullptr);
        constexpr int BOARDING_TIMEOUT_S = 30;

        while (!SignalHelper::shouldExit(g_signals)) {
            usleep(100000);

            {
                SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);

                if (shm_->state == RopewayState::EMERGENCY_STOP) {
                    Logger::info(tag_, "Ropeway emergency stop, waiting...");
                    continue;
                }

                if (shm_->state == RopewayState::CLOSING || shm_->state == RopewayState::STOPPED) {
                    Logger::info(tag_, "Ropeway closing, leaving");
                    int32_t idx = shm_->boardingQueue.findTourist(tourist_.id);
                    if (idx >= 0) {
                        shm_->boardingQueue.removeTourist(static_cast<uint32_t>(idx));
                    }
                    if (shm_->touristsInLowerStation > 0) {
                        shm_->touristsInLowerStation--;
                    }
                    changeState(TouristState::FINISHED);
                    return;
                }

                int32_t idx = shm_->boardingQueue.findTourist(tourist_.id);
                if (idx >= 0) {
                    BoardingQueueEntry& entry = shm_->boardingQueue.entries[idx];
                    if (entry.readyToBoard && entry.assignedChairId >= 0) {
                        assignedChairId_ = entry.assignedChairId;
                        Logger::info(tag_, "Assigned to chair ", assignedChairId_);

                        shm_->boardingQueue.removeTourist(static_cast<uint32_t>(idx));

                        if (shm_->touristsInLowerStation > 0) {
                            shm_->touristsInLowerStation--;
                        }
                        shm_->touristsOnPlatform++;

                        changeState(TouristState::ON_CHAIR);
                        return;
                    }
                } else {
                    Logger::perr(tag_, "Lost from boarding queue!");
                    changeState(TouristState::FINISHED);
                    return;
                }
            }

            if (time(nullptr) - waitStart > BOARDING_TIMEOUT_S) {
                Logger::info(tag_, "Boarding timeout, leaving");
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
                changeState(TouristState::FINISHED);
                return;
            }
        }
    }

    void handleOnChair() {
        Logger::info(tag_, "On chair ", assignedChairId_, ", riding up...");

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
                    Logger::info(tag_, "Released chair ", assignedChairId_);
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
        Logger::info(tag_, "Arrived at top station");

        usleep(100000 + (rand() % 200000));

        if (tourist_.type == TouristType::CYCLIST) {
            changeState(TouristState::ON_TRAIL);
        } else {
            Logger::info(tag_, "Leaving the area");
            changeState(TouristState::FINISHED);
        }
    }

    void handleOnTrail() {
        uint32_t trailTime = EnumStrings::getTrailTimeSeconds(tourist_.preferredTrail);
        Logger::info(tag_, "Cycling down trail (difficulty: ", static_cast<int>(tourist_.preferredTrail), ")");

        usleep((trailTime / 100) * 1000000);

        Logger::info(tag_, "Finished trail descent");

        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            if (shm_->acceptingNewTourists && shm_->state == RopewayState::RUNNING) {
                changeState(TouristState::WAITING_ENTRY);
                return;
            }
        }

        Logger::info(tag_, "Leaving the area");
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
    char tag_[32];
};

int main(int argc, char* argv[]) {
    ArgumentParser::TouristArgs args{};
    if (!ArgumentParser::parseTouristArgs(argc, argv, args)) {
        return 1;
    }

    SignalHelper::setup(g_signals, SignalHelper::Mode::TOURIST);
    srand(static_cast<unsigned>(time(nullptr)) ^ static_cast<unsigned>(getpid()));

    char tag[32];
    strcpy(tag, "Tourist ");
    char idBuf[16];
    char* ptr = idBuf + sizeof(idBuf) - 1;
    *ptr = '\0';
    unsigned int id = args.id;
    do { --ptr; *ptr = '0' + (id % 10); id /= 10; } while (id > 0);
    strcat(tag, ptr);

    try {
        TouristProcess process(args);
        process.run();
    } catch (const std::exception& e) {
        Logger::perr(tag, e.what());
        return 1;
    }

    return 0;
}
