#include <unistd.h>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>

#include "ipc/core/SharedMemory.h"
#include "ipc/core/Semaphore.h"
#include "ipc/core/MessageQueue.h"
#include "ipc/model/SharedRopewayState.h"
#include "entrance/CashierMessage.h"
#include "ropeway/gate/EntryGateMessage.h"
#include "tourist/Tourist.h"
#include "core/Config.h"
#include "core/Constants.h"
#include "utils/SignalHelper.h"
#include "utils/ArgumentParser.h"
#include "logging/Logger.h"
#include "utils/TimeHelper.h"

namespace {
    SignalHelper::Flags g_signals;
}

/**
 * Child thread - simply exists and dies with parent.
 * Children are represented as threads, not separate processes.
 */
class ChildThread {
public:
    ChildThread(uint32_t childId, uint32_t age, uint32_t parentId)
        : childId_{childId}, age_{age}, parentId_{parentId} {
    }

    void start() {
        thread_ = std::thread([this]() {
            Logger::info("Child", "[Thread %u] age=%u, with parent %u", childId_, age_, parentId_);
            {
                std::unique_lock lock(mtx_);
                cv_.wait(lock, [this]() { return !running_; });
            }
            Logger::debug("Child", "[Thread %u] finished with parent", childId_);
        });
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            running_ = false;
        }
        cv_.notify_one();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    uint32_t age() const { return age_; }

private:
    uint32_t childId_;
    uint32_t age_;
    uint32_t parentId_;
    bool running_{true};
    std::mutex mtx_;
    std::condition_variable cv_;
    std::thread thread_;
};

/**
 * Bike thread - represents the cyclist's bike.
 * Takes an extra slot on the chair.
 */
class BikeThread {
public:
    explicit BikeThread(uint32_t ownerId) : ownerId_{ownerId} {
    }

    void start() {
        thread_ = std::thread([this]() {
            Logger::debug("Bike", "[Thread] bike of tourist %u", ownerId_);
            {
                std::unique_lock<std::mutex> lock(mtx_);
                cv_.wait(lock, [this]() { return !running_; });
            }
            Logger::debug("Bike", "[Thread] bike stored", ownerId_);
        });
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            running_ = false;
        }
        cv_.notify_one();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    uint32_t ownerId_;
    bool running_{true};
    std::mutex mtx_;
    std::condition_variable cv_;
    std::thread thread_;
};

class TouristProcess {
public:
    TouristProcess(const ArgumentParser::TouristArgs &args)
        : shm_{SharedMemory<SharedRopewayState>::attach(args.shmKey)},
          sem_{args.semKey},
          requestQueue_{args.cashierMsgKey, "CashierReq"},
          responseQueue_{args.cashierMsgKey, "CashierResp"},
          entryRequestQueue_{args.entryGateMsgKey, "EntryReq"},
          entryResponseQueue_{args.entryGateMsgKey, "EntryResp"},
          tag_{"Tourist"} {
        tourist_.id = args.id;
        tourist_.pid = getpid();
        tourist_.age = args.age;
        tourist_.type = static_cast<TouristType>(args.type);
        tourist_.isVip = args.isVip;
        tourist_.wantsToRide = args.wantsToRide;
        tourist_.preferredTrail = static_cast<TrailDifficulty>(args.trail);
        tourist_.state = TouristState::BUYING_TICKET;

        // Determine group composition (children and bike)
        setupGroup();

        // Set simulation start time for logger
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            simulationStartTime_ = shm_->operational.openingTime;
            Logger::setSimulationStartTime(simulationStartTime_);
        }

        // Create descriptive tag
        snprintf(tagBuf_, sizeof(tagBuf_), "Tourist %u", tourist_.id);
        tag_ = tagBuf_;

        // Log tourist info
        const char* typeStr = tourist_.type == TouristType::CYCLIST ? "cyclist" : "pedestrian";
        if (tourist_.childCount > 0 && tourist_.hasBike) {
            Logger::info(tag_, "age=%u, %s with bike, %u children (slots=%u)",
                         tourist_.age, typeStr, tourist_.childCount, tourist_.slots);
        } else if (tourist_.childCount > 0) {
            Logger::info(tag_, "age=%u, %s, %u children (slots=%u)",
                         tourist_.age, typeStr, tourist_.childCount, tourist_.slots);
        } else if (tourist_.hasBike) {
            Logger::info(tag_, "age=%u, %s with bike (slots=%u)",
                         tourist_.age, typeStr, tourist_.slots);
        } else {
            Logger::info(tag_, "age=%u, %s (slots=%u)",
                         tourist_.age, typeStr, tourist_.slots);
        }
    }

    ~TouristProcess() {
        // Stop all child threads
        for (auto& child : childThreads_) {
            child->stop();
        }
        // Stop bike thread
        if (bikeThread_) {
            bikeThread_->stop();
        }
    }

    void run() {
        // Start child threads (they just exist alongside parent)
        for (auto& child : childThreads_) {
            child->start();
        }
        // Start bike thread
        if (bikeThread_) {
            bikeThread_->start();
        }

        while (tourist_.state != TouristState::FINISHED && !g_signals.exit) {
            switch (tourist_.state) {
                case TouristState::BUYING_TICKET:
                    buyTicket();
                    break;
                case TouristState::WAITING_ENTRY:
                    enterStation();
                    break;
                case TouristState::WAITING_BOARDING:
                    waitForChair();
                    break;
                case TouristState::ON_CHAIR:
                    rideChair();
                    break;
                case TouristState::AT_TOP:
                    exitAtTop();
                    break;
                case TouristState::ON_TRAIL:
                    descendTrail();
                    break;
                default:
                    break;
            }
        }

        Logger::info(tag_, "Finished (group of %u)", tourist_.slots);
    }

private:
    /**
     * Setup group composition - determine if tourist has children and/or bike.
     * Children and bikes are represented as threads.
     */
    void setupGroup() {
        // Only adults can have children
        if (tourist_.isAdult()) {
            float childRoll = static_cast<float>(rand()) / RAND_MAX;
            if (childRoll < Constants::Group::CHILD_CHANCE) {
                // Determine number of children (1 or 2)
                float twoChildRoll = static_cast<float>(rand()) / RAND_MAX;
                tourist_.childCount = (twoChildRoll < Constants::Group::TWO_CHILDREN_CHANCE) ? 2 : 1;

                // Create child threads
                for (uint32_t i = 0; i < tourist_.childCount; ++i) {
                    uint32_t childAge = 3 + (rand() % 5); // Age 3-7
                    tourist_.childAges[i] = childAge;
                    childThreads_.push_back(
                        std::make_unique<ChildThread>(tourist_.id * 100 + i, childAge, tourist_.id)
                    );
                }
            }
        }

        // Cyclists may have a bike (takes extra slot)
        if (tourist_.type == TouristType::CYCLIST) {
            float bikeRoll = static_cast<float>(rand()) / RAND_MAX;
            if (bikeRoll < Constants::Group::BIKE_CHANCE) {
                tourist_.hasBike = true;
                bikeThread_ = std::make_unique<BikeThread>(tourist_.id);
            }
        }

        // Calculate total slots needed
        tourist_.calculateSlots();
    }

    void changeState(TouristState newState) {
        Logger::info(tag_, "%s -> %s", toString(tourist_.state), toString(newState));
        tourist_.state = newState;
    }

    TicketType chooseTicketType() {
        float roll = static_cast<float>(rand()) / RAND_MAX;
        float cumulative = 0.0f;

        cumulative += Config::Ticket::SINGLE_USE_CHANCE();
        if (roll < cumulative) return TicketType::SINGLE_USE;

        cumulative += Config::Ticket::TK1_CHANCE();
        if (roll < cumulative) return TicketType::TIME_TK1;

        cumulative += Config::Ticket::TK2_CHANCE();
        if (roll < cumulative) return TicketType::TIME_TK2;

        cumulative += Config::Ticket::TK3_CHANCE();
        if (roll < cumulative) return TicketType::TIME_TK3;

        return TicketType::DAILY;
    }

    void buyTicket() {
        // Check if ropeway is accepting tourists
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            if (!shm_->operational.acceptingNewTourists) {
                Logger::info(tag_, "Ropeway closed, leaving");
                changeState(TouristState::FINISHED);
                return;
            }
        }

        // Request ticket from cashier
        TicketRequest request;
        request.touristId = tourist_.id;
        request.touristAge = tourist_.age;
        request.requestedType = chooseTicketType();
        request.requestVip = tourist_.isVip;

        Logger::info(tag_, "Requesting %s ticket...", toString(request.requestedType));

        // Acquire queue slot
        // useUndo=false: the Cashier posts the slot back after processing,
        // not this process. SEM_UNDO would cause double-increment on exit.
        if (!sem_.wait(Semaphore::Index::CASHIER_QUEUE_SLOTS, 1, false)) {
            if (g_signals.exit) {
                changeState(TouristState::FINISHED);
                return;
            }
        }

        if (!requestQueue_.send(request, CashierMsgType::REQUEST)) {
            sem_.post(Semaphore::Index::CASHIER_QUEUE_SLOTS, 1, false);
            changeState(TouristState::FINISHED);
            return;
        }

        // Wait for response
        long responseType = CashierMsgType::RESPONSE_BASE + tourist_.id;
        auto response = responseQueue_.receive(responseType);

        if (!response || !response->success) {
            Logger::info(tag_, "Ticket denied");
            changeState(TouristState::FINISHED);
            return;
        }

        tourist_.ticketId = response->ticketId;
        tourist_.hasTicket = true;
        tourist_.isVip = response->isVip;
        tourist_.ticketType = response->ticketType;
        tourist_.ticketValidUntil = response->validUntil;

        // Register tourist and group for statistics
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_STATS);

            // Register main tourist
            shm_->registerTourist(tourist_.id, tourist_.ticketId, tourist_.age,
                                  tourist_.type, tourist_.isVip, -1);

            auto &stats = shm_->stats.dailyStats;
            stats.totalTourists++;
            stats.ticketsSold++;
            stats.totalRevenueWithDiscounts += response->price;

            if (tourist_.isVip) stats.vipTourists++;
            if (tourist_.age >= Constants::Age::SENIOR_AGE_FROM) {
                stats.seniorsServed++;
            }

            if (tourist_.type == TouristType::CYCLIST) {
                stats.cyclistRides++;
            } else {
                stats.pedestrianRides++;
            }

            // Count children
            stats.childrenServed += tourist_.childCount;
            stats.totalTourists += tourist_.childCount;
        }

        Logger::info(tag_, "Got %s ticket #%u%s",
                     toString(tourist_.ticketType), tourist_.ticketId,
                     tourist_.isVip ? " [VIP]" : "");

        if (!tourist_.wantsToRide) {
            changeState(TouristState::FINISHED);
        } else {
            changeState(TouristState::WAITING_ENTRY);
        }
    }

    void enterStation() {
        EntryGateRequest request;
        request.touristId = tourist_.id;
        request.touristPid = tourist_.pid;
        request.isVip = tourist_.isVip;

        long requestType = tourist_.isVip ? EntryGateMsgType::VIP_REQUEST : EntryGateMsgType::REGULAR_REQUEST;
        uint8_t queueSlotSem = tourist_.isVip
            ? Semaphore::Index::ENTRY_QUEUE_VIP_SLOTS
            : Semaphore::Index::ENTRY_QUEUE_REGULAR_SLOTS;

        Logger::info(tag_, "Requesting entry (group of %u)%s...",
                     tourist_.slots, tourist_.isVip ? " [VIP]" : "");

        // useUndo=false: the LowerWorker posts the slot back after processing,
        // not this process. SEM_UNDO would cause double-increment on exit.
        if (!sem_.wait(queueSlotSem, 1, false)) {
            if (g_signals.exit) {
                changeState(TouristState::FINISHED);
                return;
            }
        }

        if (!entryRequestQueue_.send(request, requestType)) {
            sem_.post(queueSlotSem, 1, false);
            changeState(TouristState::FINISHED);
            return;
        }

        sem_.post(Semaphore::Index::BOARDING_QUEUE_WORK, 1, false);

        long responseType = EntryGateMsgType::RESPONSE_BASE + tourist_.id;
        auto response = entryResponseQueue_.receive(responseType);

        if (!response || !response->allowed) {
            Logger::info(tag_, "Entry denied");
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_STATS);
                uint32_t simTime = TimeHelper::getSimulatedSeconds(simulationStartTime_, shm_->operational.totalPausedSeconds);
                shm_->logGatePassage(tourist_.id, tourist_.ticketId,
                                     GateType::ENTRY, 0, false, simTime);
            }
            changeState(TouristState::FINISHED);
            return;
        }

        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_STATS);
            uint32_t simTime = TimeHelper::getSimulatedSeconds(simulationStartTime_, shm_->operational.totalPausedSeconds);
            uint32_t gateNum = tourist_.id % Constants::Gate::NUM_ENTRY_GATES;
            shm_->logGatePassage(tourist_.id, tourist_.ticketId,
                                 GateType::ENTRY, gateNum, true, simTime);
        }

        Logger::info(tag_, "Entered station (group of %u)", tourist_.slots);
        changeState(TouristState::WAITING_BOARDING);
    }

    void waitForChair() {
        // Add to boarding queue with slot count
        {
            Semaphore::ScopedLock lockCore(sem_, Semaphore::Index::SHM_OPERATIONAL);
            Semaphore::ScopedLock lockChairs(sem_, Semaphore::Index::SHM_CHAIRS);

            BoardingQueueEntry entry;
            entry.touristId = tourist_.id;
            entry.touristPid = tourist_.pid;
            entry.age = tourist_.age;
            entry.type = tourist_.type;
            entry.isVip = tourist_.isVip;
            entry.slots = tourist_.slots;
            entry.childCount = tourist_.childCount;
            entry.hasBike = tourist_.hasBike;
            entry.assignedChairId = -1;
            entry.readyToBoard = false;

            if (!shm_->chairPool.boardingQueue.addTourist(entry)) {
                Logger::error(tag_, "Queue full");
                shm_->operational.touristsInLowerStation--;
                sem_.post(Semaphore::Index::STATION_CAPACITY, 1, false);
                changeState(TouristState::FINISHED);
                return;
            }
        }

        sem_.post(Semaphore::Index::BOARDING_QUEUE_WORK, 1, false);

        Logger::info(tag_, "Waiting for chair (need %u slots)...", tourist_.slots);

        while (!g_signals.exit) {
            sem_.wait(Semaphore::Index::CHAIR_ASSIGNED, 1, true);

            bool assigned = false;
            {
                Semaphore::ScopedLock lockCore(sem_, Semaphore::Index::SHM_OPERATIONAL);
                Semaphore::ScopedLock lockChairs(sem_, Semaphore::Index::SHM_CHAIRS);
                Semaphore::ScopedLock lockStats(sem_, Semaphore::Index::SHM_STATS);

                int32_t idx = shm_->chairPool.boardingQueue.findTourist(tourist_.id);
                if (idx >= 0) {
                    BoardingQueueEntry &entry = shm_->chairPool.boardingQueue.entries[idx];
                    if (entry.readyToBoard && entry.assignedChairId >= 0) {
                        assignedChairId_ = entry.assignedChairId;
                        shm_->chairPool.boardingQueue.removeTourist(static_cast<uint32_t>(idx));
                        shm_->operational.touristsInLowerStation--;

                        uint32_t simTime = TimeHelper::getSimulatedSeconds(simulationStartTime_, shm_->operational.totalPausedSeconds);
                        uint32_t gateNum = assignedChairId_ % Constants::Gate::NUM_RIDE_GATES;
                        shm_->logGatePassage(tourist_.id, tourist_.ticketId,
                                             GateType::RIDE, gateNum, true, simTime);
                        assigned = true;
                    }
                } else {
                    Logger::error(tag_, "Lost from queue");
                    sem_.post(Semaphore::Index::STATION_CAPACITY, 1, false);
                    changeState(TouristState::FINISHED);
                    return;
                }
            }

            if (assigned) {
                Logger::info(tag_, "Assigned to chair %d (group of %u)", assignedChairId_, tourist_.slots);
                changeState(TouristState::ON_CHAIR);
                return;
            }
            // Do NOT re-post - worker wakes all tourists on each dispatch
            // Re-posting causes a race where non-assigned tourists starve assigned ones
        }

        // Interrupted - cleanup
        cleanupFromBoardingQueue();
        changeState(TouristState::FINISHED);
    }

    void cleanupFromBoardingQueue() {
        Logger::info(tag_, "Cleaning up from boarding queue");

        Semaphore::ScopedLock lockCore(sem_, Semaphore::Index::SHM_OPERATIONAL);
        Semaphore::ScopedLock lockChairs(sem_, Semaphore::Index::SHM_CHAIRS);

        int32_t idx = shm_->chairPool.boardingQueue.findTourist(tourist_.id);
        if (idx >= 0) {
            shm_->chairPool.boardingQueue.removeTourist(static_cast<uint32_t>(idx));
            shm_->operational.touristsInLowerStation--;
            sem_.post(Semaphore::Index::STATION_CAPACITY, 1, false);
        }
    }

    void rideChair() {
        Logger::info(tag_, "Riding chair %d (group of %u)...", assignedChairId_, tourist_.slots);

        usleep(Config::Chair::RIDE_DURATION_US());

        bool lastPassenger = false;
        {
            Semaphore::ScopedLock lockCore(sem_, Semaphore::Index::SHM_OPERATIONAL);
            Semaphore::ScopedLock lockChairs(sem_, Semaphore::Index::SHM_CHAIRS);
            shm_->operational.totalRidesToday++;
            shm_->operational.touristsAtUpperStation++;

            if (assignedChairId_ >= 0 && static_cast<uint32_t>(assignedChairId_) < Constants::Chair::QUANTITY) {
                Chair &chair = shm_->chairPool.chairs[assignedChairId_];
                if (chair.numPassengers > 0) {
                    chair.numPassengers--;
                }
                // Last passenger releases the chair
                if (chair.numPassengers == 0) {
                    chair.isOccupied = false;
                    if (shm_->chairPool.chairsInUse > 0) {
                        shm_->chairPool.chairsInUse--;
                    }
                    lastPassenger = true;
                }
            }
        }

        sem_.post(Semaphore::Index::STATION_CAPACITY, 1, false);
        // Wake LowerWorker whenever station capacity is freed — pending entry
        // requests may be waiting for a slot. Previously only the last passenger
        // posted here, which meant re-queued entry requests could stall.
        sem_.post(Semaphore::Index::BOARDING_QUEUE_WORK, 1, false);

        // Only last passenger releases the chair itself
        if (lastPassenger) {
            sem_.post(Semaphore::Index::CHAIRS_AVAILABLE, 1, false);
        }

        assignedChairId_ = -1;
        changeState(TouristState::AT_TOP);
    }

    void exitAtTop() {
        Logger::info(tag_, "Arrived at top (group of %u)", tourist_.slots);

        bool isCyclist = (tourist_.type == TouristType::CYCLIST);

        if (isCyclist) {
            sem_.wait(Semaphore::Index::EXIT_BIKE_TRAILS, 1, false);
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
                shm_->operational.cyclistsOnBikeTrailExit++;
            }
            Logger::info(tag_, "Exiting to bike trails");
        } else {
            sem_.wait(Semaphore::Index::EXIT_WALKING_PATH, 1, false);
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
                shm_->operational.pedestriansOnWalkingExit++;
            }
            Logger::info(tag_, "Exiting to walking path");
        }

        usleep(Constants::Delay::EXIT_ROUTE_TRANSITION_US);

        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            if (shm_->operational.touristsAtUpperStation > 0) {
                shm_->operational.touristsAtUpperStation--;
            }
            if (isCyclist && shm_->operational.cyclistsOnBikeTrailExit > 0) {
                shm_->operational.cyclistsOnBikeTrailExit--;
            } else if (!isCyclist && shm_->operational.pedestriansOnWalkingExit > 0) {
                shm_->operational.pedestriansOnWalkingExit--;
            }
        }

        if (isCyclist) {
            sem_.post(Semaphore::Index::EXIT_BIKE_TRAILS, 1, false);
        } else {
            sem_.post(Semaphore::Index::EXIT_WALKING_PATH, 1, false);
        }

        changeState(TouristState::ON_TRAIL);
    }

    void descendTrail() {
        if (tourist_.type == TouristType::CYCLIST) {
            uint32_t trailDuration;
            const char *trailName;
            switch (tourist_.preferredTrail) {
                case TrailDifficulty::MEDIUM:
                    trailDuration = Config::Trail::DURATION_MEDIUM_US();
                    trailName = "T2 (medium)";
                    break;
                case TrailDifficulty::HARD:
                    trailDuration = Config::Trail::DURATION_HARD_US();
                    trailName = "T3 (hard)";
                    break;
                default:
                    trailDuration = Config::Trail::DURATION_EASY_US();
                    trailName = "T1 (easy)";
                    break;
            }
            Logger::info(tag_, "Cycling down trail %s...", trailName);
            usleep(trailDuration);
        } else {
            Logger::info(tag_, "Walking down trail...");
            usleep(Config::Trail::DURATION_EASY_US() / 2);
        }

        tourist_.ridesCompleted++;

        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_STATS);
            shm_->recordRide(tourist_.id);
        }

        Logger::info(tag_, "Trail complete (rides: %u)", tourist_.ridesCompleted);

        const time_t paused = shm_->operational.totalPausedSeconds;
        if (tourist_.canRideAgain() && tourist_.isTicketValid(paused)) {
            Logger::info(tag_, "Ticket still valid, going for another ride!");
            changeState(TouristState::WAITING_ENTRY);
        } else if (tourist_.canRideAgain() && !tourist_.isTicketValid(paused)) {
            Logger::info(tag_, "Time ticket expired (completed %u rides)", tourist_.ridesCompleted);
            changeState(TouristState::FINISHED);
        } else {
            Logger::info(tag_, "Single-use ticket completed");
            changeState(TouristState::FINISHED);
        }
    }

    Tourist tourist_;
    SharedMemory<SharedRopewayState> shm_;
    Semaphore sem_;
    MessageQueue<TicketRequest> requestQueue_;
    MessageQueue<TicketResponse> responseQueue_;
    MessageQueue<EntryGateRequest> entryRequestQueue_;
    MessageQueue<EntryGateResponse> entryResponseQueue_;
    int32_t assignedChairId_{-1};
    time_t simulationStartTime_{0};
    char tagBuf_[32];
    const char *tag_;

    // Group threads (children and bike)
    std::vector<std::unique_ptr<ChildThread>> childThreads_;
    std::unique_ptr<BikeThread> bikeThread_;
};

int main(int argc, char *argv[]) {
    ArgumentParser::TouristArgs args{};
    if (!ArgumentParser::parseTouristArgs(argc, argv, args)) {
        return 1;
    }

    SignalHelper::setup(g_signals, true);
    srand(static_cast<unsigned>(time(nullptr)) ^ static_cast<unsigned>(getpid()) ^ (args.id * 31337));

    try {
        Config::loadEnvFile();
        Logger::initCentralized(args.shmKey, args.semKey, args.logMsgKey);

        TouristProcess process(args);
        process.run();

        Logger::cleanupCentralized();
    } catch (const std::exception &e) {
        Logger::error("Tourist", "Exception: %s", e.what());
        return 1;
    }

    return 0;
}
