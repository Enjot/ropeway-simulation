#include <cstring>
#include <unistd.h>
#include <ctime>
#include <cstdlib>

#include "ipc/core/SharedMemory.h"
#include "ipc/core/Semaphore.h"
#include "ipc/core/MessageQueue.h"
#include "ipc/model/SharedRopewayState.h"
#include "entrance/CashierMessage.h"
#include "core/Config.h"
#include "utils/SignalHelper.h"
#include "utils/ArgumentParser.h"
#include "logging/Logger.h"

namespace {
    SignalHelper::Flags g_signals;
    constexpr const char *TAG = "Cashier";
    constexpr auto SRC = Logger::Source::Cashier;
}

class CashierProcess {
public:
    CashierProcess(const ArgumentParser::CashierArgs &args)
        : shm_{SharedMemory<SharedRopewayState>::attach(args.shmKey)},
          sem_{args.semKey},
          requestQueue_{args.cashierMsgKey, "CashierReq"},
          responseQueue_{args.cashierMsgKey, "CashierResp"},
          nextTicketId_{1},
          isClosed_{false} {
        // Set simulation start time for logger
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            Logger::setSimulationStartTime(shm_->operational.openingTime);
        }

        Logger::info(SRC, TAG, "Started (PID: %d)", getpid());

        // Signal readiness
        sem_.post(Semaphore::Index::CASHIER_READY, 1, false);
    }

    void run() {
        Logger::info(SRC, TAG, "Ready to serve");

        while (!g_signals.exit) {
            auto request = requestQueue_.receive(CashierMsgType::REQUEST);
            if (!request) {
                continue;
            }

            // Check for closing notification (sentinel touristId = 0)
            if (request->touristId == CASHIER_CLOSING_SENTINEL) {
                isClosed_ = true;
                Logger::warn(SRC, TAG, "Cashier closing - no longer accepting ticket requests");
                continue;
            }

            processRequest(*request);

            // Release queue slot so another tourist can send request
            sem_.post(Semaphore::Index::CASHIER_QUEUE_SLOTS, 1, false);
        }

        Logger::debug(SRC, TAG, "Cashier process exiting");
    }

private:
    void processRequest(const TicketRequest &request) {
        Logger::info(SRC, TAG, "Processing Tourist %u (age %u)", request.touristId, request.touristAge);

        TicketResponse response;
        response.touristId = request.touristId;

        // Check if closed (using internal flag set by CLOSING message)
        if (isClosed_) {
            response.success = false;
            strcpy(response.message, "Ropeway closed");
            sendResponse(response, request.touristId);
            Logger::info(SRC, TAG, "Rejected Tourist %u: closed", request.touristId);
            return;
        }

        // Calculate price with discounts
        double basePrice = TicketPricing::getPrice(request.requestedType);
        double touristDiscount = 0.0;

        if (request.touristAge < Constants::Discount::CHILD_DISCOUNT_AGE) {
            touristDiscount = Constants::Discount::CHILD_DISCOUNT;
        } else if (request.touristAge >= Constants::Age::SENIOR_AGE_FROM) {
            touristDiscount = Constants::Discount::SENIOR_DISCOUNT;
        }

        // Calculate tourist price (main tourist process)
        double touristPrice = basePrice * (1.0 - touristDiscount);

        // Calculate children's prices (25% discount for each child under 10)
        double childrenPrice = 0.0;
        for (uint32_t i = 0; i < request.childCount; ++i) {
            childrenPrice += basePrice * (1.0 - Constants::Discount::CHILD_DISCOUNT);
        }

        // Issue ticket
        response.success = true;
        response.ticketId = nextTicketId_++;
        response.ticketType = request.requestedType;
        response.isVip = request.requestVip;
        response.price = touristPrice + childrenPrice;
        response.discount = touristDiscount;
        response.validFrom = time(nullptr) - shm_->operational.totalPausedSeconds;

        // Set validity based on ticket type
        switch (request.requestedType) {
            case TicketType::SINGLE_USE:
                response.validUntil = response.validFrom + 24 * 3600; // Valid all day
                break;
            case TicketType::TIME_TK1:
                response.validUntil = response.validFrom + Config::Ticket::TK1_DURATION_SEC();
                break;
            case TicketType::TIME_TK2:
                response.validUntil = response.validFrom + Config::Ticket::TK2_DURATION_SEC();
                break;
            case TicketType::TIME_TK3:
                response.validUntil = response.validFrom + Config::Ticket::TK3_DURATION_SEC();
                break;
            case TicketType::DAILY:
                response.validUntil = response.validFrom + Config::Ticket::DAILY_DURATION_SEC();
                break;
        }
        strcpy(response.message, "Ticket issued");

        sendResponse(response, request.touristId);
        Logger::info(SRC, TAG, "Sold %s ticket #%u to Tourist %u",
                     toString(response.ticketType), response.ticketId, request.touristId);
    }

    void sendResponse(const TicketResponse &response, uint32_t touristId) {
        long responseType = CashierMsgType::RESPONSE_BASE + touristId;
        // Blocking send — the tourist is waiting on receive, so this
        // unblocks as soon as the system-wide queue limit has room.
        responseQueue_.send(response, responseType);
    }

    SharedMemory<SharedRopewayState> shm_;
    Semaphore sem_;
    MessageQueue<TicketRequest> requestQueue_;
    MessageQueue<TicketResponse> responseQueue_;
    uint32_t nextTicketId_;
    bool isClosed_;
};

int main(int argc, char *argv[]) {
    ArgumentParser::CashierArgs args{};
    if (!ArgumentParser::parseCashierArgs(argc, argv, args)) {
        return 1;
    }

    SignalHelper::setupChildProcess(g_signals, false);
    srand(static_cast<unsigned>(time(nullptr)) ^ static_cast<unsigned>(getpid()));

    try {
        Config::loadEnvFile();
        Logger::initCentralized(args.shmKey, args.semKey, args.logMsgKey);

        CashierProcess cashier(args);
        cashier.run();

        Logger::cleanupCentralized();
    } catch (const std::exception &e) {
        Logger::error(SRC, TAG, "Exception: %s", e.what());
        return 1;
    }

    return 0;
}