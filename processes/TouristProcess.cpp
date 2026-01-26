#include <unistd.h>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <vector>

#include "ipc/core/SharedMemory.hpp"
#include "ipc/core/Semaphore.hpp"
#include "ipc/core/MessageQueue.hpp"
#include "ipc/RopewaySystemState.hpp"
#include "ipc/message/CashierMessage.hpp"
#include "structures/Tourist.hpp"
#include "Config.hpp"
#include "utils/SignalHelper.hpp"
#include "utils/ArgumentParser.hpp"
#include "utils/Logger.hpp"

namespace {
    SignalHelper::Flags g_signals;
}

class TouristProcess {
public:
    TouristProcess(const ArgumentParser::TouristArgs &args)
        : shm_{SharedMemory<RopewaySystemState>::attach(args.shmKey)},
          sem_{args.semKey},
          requestQueue_{args.cashierMsgKey, "CashierReq"},
          responseQueue_{args.cashierMsgKey, "CashierResp"},
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
        tourist_.state = TouristState::BUYING_TICKET;

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

    void buyTicket() {
        usleep(50000); // Small delay

        // Check if ropeway is accepting tourists
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
            if (!shm_->core.acceptingNewTourists) {
                Logger::info(tag_, "Ropeway closed, leaving");
                changeState(TouristState::FINISHED);
                return;
            }
        }

        // Request ticket from cashier
        TicketRequest request;
        request.touristId = tourist_.id;
        request.touristAge = tourist_.age;
        request.requestedType = TicketType::SINGLE_USE;
        request.requestVip = tourist_.isVip;

        Logger::info(tag_, "Requesting ticket...");

        if (!requestQueue_.send(request, CashierMsgType::REQUEST)) {
            Logger::error(tag_, "Failed to send ticket request");
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

        Logger::info(tag_, "Got ticket #%u%s", tourist_.ticketId, tourist_.isVip ? " [VIP]" : "");

        if (!tourist_.wantsToRide) {
            changeState(TouristState::FINISHED);
        } else {
            // Spawn children after getting ticket (parent only)
            spawnChildren();
            changeState(TouristState::WAITING_ENTRY);
        }
    }

    void spawnChildren() {
        // Only adults without guardians can spawn children
        if (numChildren_ == 0 || tourist_.guardianId >= 0) {
            return;
        }

        for (uint32_t i = 0; i < numChildren_; ++i) {
            // Get unique ID for child from shared memory
            uint32_t childId;
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
                childId = ++shm_->stats.nextTouristId;
            }

            pid_t childPid = fork();

            if (childPid == -1) {
                Logger::error(tag_, "Failed to fork child");
                continue;
            }

            if (childPid == 0) {
                // === CHILD PROCESS ===
                // Modify tourist data for child
                tourist_.id = childId;
                tourist_.pid = getpid();
                tourist_.age = 3 + (rand() % 5);  // Age 3-7 (needs supervision)
                tourist_.type = TouristType::PEDESTRIAN;
                tourist_.guardianId = static_cast<int32_t>(args_.id);  // Parent's ID
                tourist_.state = TouristState::WAITING_ENTRY;

                // Reset for child
                numChildren_ = 0;
                childPids_.clear();

                // Update tag for logging
                snprintf(tagBuf_, sizeof(tagBuf_), "Tourist %u", tourist_.id);
                tag_ = tagBuf_;

                Logger::info(tag_, "Child started (age=%u, guardian=%d)",
                             tourist_.age, tourist_.guardianId);

                // Child continues from WAITING_ENTRY state
                return;
            }

            // === PARENT PROCESS ===
            childPids_.push_back(childPid);
            tourist_.dependentIds[i] = static_cast<int32_t>(childId);
            tourist_.dependentCount++;

            Logger::info(tag_, "[SPAWN] child %u (PID: %d, age will be assigned)", childId, childPid);
        }
    }

    void enterStation() {
        Logger::info(tag_, "Waiting to enter station...");

        // Wait for station capacity
        sem_.wait(Semaphore::Index::STATION_CAPACITY);

        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
            shm_->core.touristsInLowerStation++;
        }

        Logger::info(tag_, "Entered station");
        changeState(TouristState::WAITING_BOARDING);
    }

    void waitForChair() {
        // Add to boarding queue
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
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
                shm_->core.touristsInLowerStation--;
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

            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);

            int32_t idx = shm_->chairPool.boardingQueue.findTourist(tourist_.id);
            if (idx >= 0) {
                BoardingQueueEntry &entry = shm_->chairPool.boardingQueue.entries[idx];
                if (entry.readyToBoard && entry.assignedChairId >= 0) {
                    assignedChairId_ = entry.assignedChairId;
                    shm_->chairPool.boardingQueue.removeTourist(static_cast<uint32_t>(idx));
                    shm_->core.touristsInLowerStation--;

                    Logger::info(tag_, "Assigned to chair %d", assignedChairId_);
                    changeState(TouristState::ON_CHAIR);
                    return;
                }
            } else {
                Logger::error(tag_, "Lost from queue");
                sem_.post(Semaphore::Index::STATION_CAPACITY, false);
                changeState(TouristState::FINISHED);
                return;
            }
        }
    }

    void rideChair() {
        Logger::info(tag_, "Riding chair %d...", assignedChairId_);

        usleep(Config::Chair::RIDE_DURATION_US);

        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
            shm_->core.totalRidesToday++;

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
        usleep(100000);
        changeState(TouristState::ON_TRAIL);
    }

    void descendTrail() {
        if (tourist_.type == TouristType::CYCLIST) {
            Logger::info(tag_, "Cycling down trail...");
            usleep(Config::Trail::DURATION_EASY_US);
        } else {
            Logger::info(tag_, "Walking down trail...");
            usleep(Config::Trail::DURATION_EASY_US / 2);  // Pedestrians are faster (no bike)
        }
        Logger::info(tag_, "Trail complete");
        changeState(TouristState::FINISHED);
    }

    Tourist tourist_;
    SharedMemory<RopewaySystemState> shm_;
    Semaphore sem_;
    MessageQueue<TicketRequest> requestQueue_;
    MessageQueue<TicketResponse> responseQueue_;
    ArgumentParser::TouristArgs args_;
    uint32_t numChildren_;
    std::vector<pid_t> childPids_;
    int32_t assignedChairId_{-1};
    char tagBuf_[32];
    const char* tag_;
};

int main(int argc, char *argv[]) {
    ArgumentParser::TouristArgs args{};
    if (!ArgumentParser::parseTouristArgs(argc, argv, args)) {
        return 1;
    }

    SignalHelper::setup(g_signals, true);
    srand(static_cast<unsigned>(time(nullptr)) ^ static_cast<unsigned>(getpid()));

    try {
        TouristProcess process(args);
        process.run();
    } catch (const std::exception &e) {
        Logger::error("Tourist", "Exception: %s", e.what());
        return 1;
    }

    return 0;
}
