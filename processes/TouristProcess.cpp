#include <unistd.h>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <vector>

#include "ipc/core/SharedMemory.hpp"
#include "ipc/core/Semaphore.hpp"
#include "ipc/core/MessageQueue.hpp"
#include "../ipc/model/SharedRopewayState.hpp"
#include "ipc/message/CashierMessage.hpp"
#include "ipc/message/EntryGateMessage.hpp"
#include "structures/Tourist.hpp"
#include "Config.hpp"
#include "utils/SignalHelper.hpp"
#include "utils/ArgumentParser.hpp"
#include "utils/Logger.hpp"
#include "utils/TimeHelper.hpp"

namespace {
    SignalHelper::Flags g_signals;

    // Retry configuration for message queue operations under load
    constexpr uint32_t MAX_SEND_RETRIES = 5;
    constexpr uint32_t INITIAL_RETRY_DELAY_US = 10000;  // 10ms
    constexpr uint32_t MAX_RETRY_DELAY_US = 500000;     // 500ms
}

class TouristProcess {
public:
    TouristProcess(const ArgumentParser::TouristArgs &args)
        : shm_{SharedMemory<SharedRopewayState>::attach(args.shmKey)},
          sem_{args.semKey},
          requestQueue_{args.cashierMsgKey, "CashierReq"},
          responseQueue_{args.cashierMsgKey, "CashierResp"},
          entryRequestQueue_{args.entryGateMsgKey, "EntryReq"},
          entryResponseQueue_{args.entryGateMsgKey, "EntryResp"},
          args_{args},
          numChildren_{args.numChildren},
          tag_{"Tourist"} {

        tourist_.id = args.id;
        tourist_.pid = getpid();
        tourist_.age = args.age;
        tourist_.type = static_cast<TouristType>(args.type);
        tourist_.isVip = args.isVip;
        tourist_.wantsToRide = args.wantsToRide;
        tourist_.guardianId = args.guardianId;
        tourist_.preferredTrail = static_cast<TrailDifficulty>(args.trail);
        tourist_.state = TouristState::BUYING_TICKET;

        // Set simulation start time for logger (read from shared memory)
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            simulationStartTime_ = shm_->operational.openingTime;
            Logger::setSimulationStartTime(simulationStartTime_);
        }

        // Create descriptive tag based on role
        if (tourist_.guardianId >= 0) {
            snprintf(tagBuf_, sizeof(tagBuf_), "Child %u", tourist_.id);
            Logger::info(tagBuf_, "[CHILD] age=%u, guardian=%d",
                         tourist_.age, tourist_.guardianId);
        } else if (numChildren_ > 0) {
            snprintf(tagBuf_, sizeof(tagBuf_), "Guardian %u", tourist_.id);
            Logger::info(tagBuf_, "[GUARDIAN] age=%u, %s, will have %u children",
                         tourist_.age,
                         tourist_.type == TouristType::CYCLIST ? "cyclist" : "pedestrian",
                         numChildren_);
        } else {
            snprintf(tagBuf_, sizeof(tagBuf_), "Tourist %u", tourist_.id);
            Logger::info(tagBuf_, "age=%u, %s",
                         tourist_.age,
                         tourist_.type == TouristType::CYCLIST ? "cyclist" : "pedestrian");
        }
        tag_ = tagBuf_;
    }

    void run() {
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

        // Wait for children before finishing
        for (pid_t childPid : childPids_) {
            int status;
            waitpid(childPid, &status, 0);
        }

        Logger::info(tag_, "Finished");
    }

private:
    void changeState(TouristState newState) {
        Logger::info(tag_, "%s -> %s", toString(tourist_.state), toString(newState));
        tourist_.state = newState;
    }

    /**
     * Send message with retry logic for handling queue saturation.
     * Uses exponential backoff to prevent thundering herd.
     */
    template<typename T, typename Q>
    bool sendWithRetry(Q& queue, const T& message, long type, const char* description) {
        uint32_t delayUs = INITIAL_RETRY_DELAY_US;

        for (uint32_t attempt = 0; attempt < MAX_SEND_RETRIES; ++attempt) {
            if (queue.send(message, type)) {
                return true;
            }

            if (g_signals.exit) {
                return false;
            }

            if (attempt < MAX_SEND_RETRIES - 1) {
                Logger::debug(tag_, "Queue full, retry %u/%u for %s (delay: %ums)",
                             attempt + 1, MAX_SEND_RETRIES, description, delayUs / 1000);
                usleep(delayUs);
                // Exponential backoff with jitter
                delayUs = std::min(delayUs * 2 + (rand() % 10000), MAX_RETRY_DELAY_US);
            }
        }

        Logger::error(tag_, "Failed to send %s after %u retries", description, MAX_SEND_RETRIES);
        return false;
    }

    TicketType chooseTicketType() {
        float roll = static_cast<float>(rand()) / RAND_MAX;
        float cumulative = 0.0f;

        cumulative += Config::Ticket::SINGLE_USE_CHANCE;
        if (roll < cumulative) return TicketType::SINGLE_USE;

        cumulative += Config::Ticket::TK1_CHANCE;
        if (roll < cumulative) return TicketType::TIME_TK1;

        cumulative += Config::Ticket::TK2_CHANCE;
        if (roll < cumulative) return TicketType::TIME_TK2;

        cumulative += Config::Ticket::TK3_CHANCE;
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
        // Children use same ticket type as guardian, adults choose randomly
        request.requestedType = (tourist_.guardianId >= 0) ? tourist_.ticketType : chooseTicketType();
        request.requestVip = tourist_.isVip;

        Logger::info(tag_, "Requesting %s ticket%s...",
                     toString(request.requestedType),
                     (tourist_.guardianId >= 0) ? " (child discount)" : "");

        if (!sendWithRetry(requestQueue_, request, CashierMsgType::REQUEST, "ticket request")) {
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

        // Register tourist for daily statistics tracking
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_STATS);
            shm_->registerTourist(tourist_.id, tourist_.ticketId, tourist_.age,
                                  tourist_.type, tourist_.isVip, tourist_.guardianId);

            // Update daily statistics
            auto& stats = shm_->stats.dailyStats;
            stats.totalTourists++;
            stats.ticketsSold++;
            stats.totalRevenueWithDiscounts += response->price;

            if (tourist_.isVip) stats.vipTourists++;
            if (tourist_.age < Config::Discount::CHILD_DISCOUNT_AGE) {
                stats.childrenServed++;
            } else if (tourist_.age >= Config::Age::SENIOR_AGE_FROM) {
                stats.seniorsServed++;
            }

            if (tourist_.type == TouristType::CYCLIST) {
                stats.cyclistRides++;
            } else {
                stats.pedestrianRides++;
            }
        }

        Logger::info(tag_, "Got %s ticket #%u%s",
                     toString(tourist_.ticketType), tourist_.ticketId,
                     tourist_.isVip ? " [VIP]" : "");

        if (!tourist_.wantsToRide) {
            changeState(TouristState::FINISHED);
        } else {
            // Spawn children after getting ticket (parent only)
            // Returns true if this process became a child (needs to buy own ticket)
            if (spawnChildren()) {
                return;  // Child will go through buyTicket() in main loop
            }
            changeState(TouristState::WAITING_ENTRY);
        }
    }

    // Returns true if this process is now a child (after fork)
    bool spawnChildren() {
        // Only adults without guardians can spawn children
        if (numChildren_ == 0 || tourist_.guardianId >= 0) {
            return false;
        }

        for (uint32_t i = 0; i < numChildren_; ++i) {
            // Get unique ID for child from shared memory
            uint32_t childId;
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_STATS);
                childId = ++shm_->stats.nextTouristId;
            }

            pid_t childPid = fork();

            if (childPid == -1) {
                Logger::error(tag_, "Failed to fork child");
                continue;
            }

            if (childPid == 0) {
                // === CHILD PROCESS ===
                // Save parent's ticket type for child to use
                TicketType parentTicketType = tourist_.ticketType;

                // Modify tourist data for child
                tourist_.id = childId;
                tourist_.pid = getpid();
                tourist_.age = 3 + (rand() % 5);  // Age 3-7 (needs supervision)
                tourist_.type = TouristType::PEDESTRIAN;
                tourist_.guardianId = static_cast<int32_t>(args_.id);  // Parent's ID
                tourist_.ticketType = parentTicketType;  // Use same ticket type as guardian
                tourist_.hasTicket = false;
                tourist_.state = TouristState::BUYING_TICKET;  // Children also buy tickets (with 25% discount)

                // Reset for child
                numChildren_ = 0;
                childPids_.clear();

                // Update tag for logging
                snprintf(tagBuf_, sizeof(tagBuf_), "Child %u", tourist_.id);
                tag_ = tagBuf_;

                Logger::info(tag_, "Started (age=%u, guardian=%d)",
                             tourist_.age, tourist_.guardianId);

                // Child needs to buy ticket, return to main loop
                return true;
            }

            // === PARENT PROCESS ===
            childPids_.push_back(childPid);
            tourist_.dependentIds[i] = static_cast<int32_t>(childId);
            tourist_.dependentCount++;

            Logger::info(tag_, "[SPAWN] child %u (PID: %d, age will be assigned)", childId, childPid);
        }
        return false;  // Parent continues normally
    }

    void enterStation() {
        // Send entry request to LowerStationWorker via message queue
        // VIPs use lower mtype for priority (will be processed first)
        EntryGateRequest request;
        request.touristId = tourist_.id;
        request.touristPid = tourist_.pid;
        request.isVip = tourist_.isVip;

        long requestType = tourist_.isVip ? EntryGateMsgType::VIP_REQUEST : EntryGateMsgType::REGULAR_REQUEST;
        Logger::info(tag_, "Requesting entry%s...", tourist_.isVip ? " [VIP PRIORITY]" : "");

        if (!sendWithRetry(entryRequestQueue_, request, requestType, "entry request")) {
            changeState(TouristState::FINISHED);
            return;
        }

        // Signal worker there's work (unified signal for both entry and boarding)
        sem_.post(Semaphore::Index::BOARDING_QUEUE_WORK, false);

        // Wait for response from LowerStationWorker
        long responseType = EntryGateMsgType::RESPONSE_BASE + tourist_.id;
        auto response = entryResponseQueue_.receive(responseType);

        if (!response || !response->allowed) {
            Logger::info(tag_, "Entry denied");
            // Log denied entry gate passage
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_STATS);
                uint32_t simTime = TimeHelper::getSimulatedSeconds(simulationStartTime_);
                shm_->logGatePassage(tourist_.id, tourist_.ticketId,
                                     GateType::ENTRY, 0, false, simTime);
            }
            changeState(TouristState::FINISHED);
            return;
        }

        // Log successful entry gate passage
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_STATS);
            uint32_t simTime = TimeHelper::getSimulatedSeconds(simulationStartTime_);
            uint32_t gateNum = tourist_.id % Config::Gate::NUM_ENTRY_GATES;
            shm_->logGatePassage(tourist_.id, tourist_.ticketId,
                                 GateType::ENTRY, gateNum, true, simTime);
        }

        Logger::info(tag_, "Entered station%s", tourist_.isVip ? " [VIP]" : "");
        changeState(TouristState::WAITING_BOARDING);
    }

    void waitForChair() {
        // Add to boarding queue
        {
            // Lock ordering: SHM_OPERATIONAL first, then SHM_CHAIRS (for touristsInLowerStation on error)
            Semaphore::ScopedLock lockCore(sem_, Semaphore::Index::SHM_OPERATIONAL);
            Semaphore::ScopedLock lockChairs(sem_, Semaphore::Index::SHM_CHAIRS);
            BoardingQueueEntry entry;
            entry.touristId = tourist_.id;
            entry.touristPid = tourist_.pid;
            entry.age = tourist_.age;
            entry.type = tourist_.type;
            entry.guardianId = tourist_.guardianId;
            entry.needsSupervision = tourist_.needsSupervision();
            entry.isAdult = tourist_.isAdult();
            entry.dependentCount = tourist_.dependentCount;
            entry.assignedChairId = -1;
            entry.readyToBoard = false;

            if (!shm_->chairPool.boardingQueue.addTourist(entry)) {
                Logger::error(tag_, "Queue full");
                shm_->operational.touristsInLowerStation--;
                sem_.post(Semaphore::Index::STATION_CAPACITY, false);
                changeState(TouristState::FINISHED);
                return;
            }
        }

        // Signal worker there's work
        sem_.post(Semaphore::Index::BOARDING_QUEUE_WORK, false);

        Logger::info(tag_, "Waiting for chair...");

        // Wait for chair assignment
        while (!g_signals.exit) {
            sem_.wait(Semaphore::Index::CHAIR_ASSIGNED);

            bool assigned = false;
            {
                // Lock ordering: SHM_OPERATIONAL, SHM_CHAIRS, SHM_STATS
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

                        // Log ride gate passage
                        uint32_t simTime = TimeHelper::getSimulatedSeconds(simulationStartTime_);
                        uint32_t gateNum = assignedChairId_ % Config::Gate::NUM_RIDE_GATES;
                        shm_->logGatePassage(tourist_.id, tourist_.ticketId,
                                             GateType::RIDE, gateNum, true, simTime);

                        assigned = true;
                    }
                } else {
                    Logger::error(tag_, "Lost from queue");
                    sem_.post(Semaphore::Index::STATION_CAPACITY, false);
                    changeState(TouristState::FINISHED);
                    return;
                }
            }

            if (assigned) {
                Logger::info(tag_, "Assigned to chair %d", assignedChairId_);
                changeState(TouristState::ON_CHAIR);
                return;
            }

            // Not assigned - re-post signal for another waiting tourist
            // This prevents signal being consumed by wrong tourist (race condition)
            sem_.post(Semaphore::Index::CHAIR_ASSIGNED, false);
        }
    }

    void rideChair() {
        Logger::info(tag_, "Riding chair %d...", assignedChairId_);

        usleep(Config::Chair::RIDE_DURATION_US);

        {
            // Lock ordering: SHM_OPERATIONAL first, then SHM_CHAIRS
            Semaphore::ScopedLock lockCore(sem_, Semaphore::Index::SHM_OPERATIONAL);
            Semaphore::ScopedLock lockChairs(sem_, Semaphore::Index::SHM_CHAIRS);
            shm_->operational.totalRidesToday++;

            // Release chair
            if (assignedChairId_ >= 0 && static_cast<uint32_t>(assignedChairId_) < Config::Chair::QUANTITY) {
                Chair &chair = shm_->chairPool.chairs[assignedChairId_];
                chair.isOccupied = false;
                chair.numPassengers = 0;
                if (shm_->chairPool.chairsInUse > 0) {
                    shm_->chairPool.chairsInUse--;
                }
            }
        }

        sem_.post(Semaphore::Index::STATION_CAPACITY, false);
        assignedChairId_ = -1;

        changeState(TouristState::AT_TOP);
    }

    void exitAtTop() {
        Logger::info(tag_, "Arrived at top");

        // Upper station has 2 exit routes (one-way traffic)
        // Route A: Cyclists exit to bike trails
        // Route B: Pedestrians exit to walking paths
        if (tourist_.type == TouristType::CYCLIST) {
            Logger::info(tag_, "Exiting via Route A (cyclists)");
        } else {
            Logger::info(tag_, "Exiting via Route B (pedestrians)");
        }

        usleep(100000);
        changeState(TouristState::ON_TRAIL);
    }

    void descendTrail() {
        if (tourist_.type == TouristType::CYCLIST) {
            // Select trail duration based on difficulty preference (T1 < T2 < T3)
            uint32_t trailDuration;
            const char* trailName;
            switch (tourist_.preferredTrail) {
                case TrailDifficulty::MEDIUM:
                    trailDuration = Config::Trail::DURATION_MEDIUM_US;
                    trailName = "T2 (medium)";
                    break;
                case TrailDifficulty::HARD:
                    trailDuration = Config::Trail::DURATION_HARD_US;
                    trailName = "T3 (hard)";
                    break;
                default:
                    trailDuration = Config::Trail::DURATION_EASY_US;
                    trailName = "T1 (easy)";
                    break;
            }
            Logger::info(tag_, "Cycling down trail %s...", trailName);
            usleep(trailDuration);
        } else {
            Logger::info(tag_, "Walking down trail...");
            usleep(Config::Trail::DURATION_EASY_US / 2);  // Pedestrians are faster (no bike)
        }

        tourist_.ridesCompleted++;

        // Record completed ride in statistics
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_STATS);
            shm_->recordRide(tourist_.id);
        }

        Logger::info(tag_, "Trail complete (rides: %u)", tourist_.ridesCompleted);

        // Check if we can ride again (time-based/daily tickets)
        if (tourist_.canRideAgain() && tourist_.isTicketValid()) {
            Logger::info(tag_, "Ticket still valid, going for another ride!");
            changeState(TouristState::WAITING_ENTRY);
        } else if (tourist_.canRideAgain() && !tourist_.isTicketValid()) {
            // Time-based ticket (Tk1/Tk2/Tk3) ran out of time
            Logger::info(tag_, "Time ticket expired (completed %u rides)", tourist_.ridesCompleted);
            changeState(TouristState::FINISHED);
        } else {
            // Single-use ticket - only 1 ride allowed
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
    ArgumentParser::TouristArgs args_;
    uint32_t numChildren_;
    std::vector<pid_t> childPids_;
    int32_t assignedChairId_{-1};
    time_t simulationStartTime_{0};
    char tagBuf_[32];
    const char* tag_;
};

int main(int argc, char *argv[]) {
    ArgumentParser::TouristArgs args{};
    if (!ArgumentParser::parseTouristArgs(argc, argv, args)) {
        return 1;
    }

    SignalHelper::setup(g_signals, true);
    // Better seeding using tourist ID, PID, and time for variety
    srand(static_cast<unsigned>(time(nullptr)) ^ static_cast<unsigned>(getpid()) ^ (args.id * 31337));

    try {
        TouristProcess process(args);
        process.run();
    } catch (const std::exception &e) {
        Logger::error("Tourist", "Exception: %s", e.what());
        return 1;
    }

    return 0;
}
