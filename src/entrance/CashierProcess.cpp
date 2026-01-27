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
}

class CashierProcess {
public:
    CashierProcess(const ArgumentParser::CashierArgs &args)
        : shm_{SharedMemory<SharedRopewayState>::attach(args.shmKey)},
          sem_{args.semKey},
          requestQueue_{args.cashierMsgKey, "CashierReq"},
          responseQueue_{args.cashierMsgKey, "CashierResp"},
          nextTicketId_{1} {
        // Set simulation start time for logger
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            Logger::setSimulationStartTime(shm_->operational.openingTime);
        }

        Logger::info(TAG, "Started (PID: %d)", getpid());

        // Signal readiness
        sem_.post(Semaphore::Index::CASHIER_READY, false);
    }

    void run() {
        Logger::info(TAG, "Ready to serve");

        while (!g_signals.exit) {
            auto request = requestQueue_.receive(CashierMsgType::REQUEST);
            if (!request) {
                continue;
            }

            processRequest(*request);
        }

        Logger::warn(TAG, "Shutting down");
    }

private:
    void processRequest(const TicketRequest &request) {
        Logger::info(TAG, "Processing Tourist %u (age %u)", request.touristId, request.touristAge);

        TicketResponse response;
        response.touristId = request.touristId;

        // Check if accepting
        bool accepting;
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHM_OPERATIONAL);
            accepting = shm_->operational.acceptingNewTourists;
        }

        if (!accepting) {
            response.success = false;
            strcpy(response.message, "Ropeway closed");
            sendResponse(response, request.touristId);
            Logger::info(TAG, "Rejected Tourist %u: closed", request.touristId);
            return;
        }

        // Calculate price with discounts
        double basePrice = TicketPricing::getPrice(request.requestedType);
        double discount = 0.0;

        if (request.touristAge < Constants::Discount::CHILD_DISCOUNT_AGE) {
            discount = Constants::Discount::CHILD_DISCOUNT;
        } else if (request.touristAge >= Constants::Age::SENIOR_AGE_FROM) {
            discount = Constants::Discount::SENIOR_DISCOUNT;
        }

        // Issue ticket
        response.success = true;
        response.ticketId = nextTicketId_++;
        response.ticketType = request.requestedType;
        response.isVip = request.requestVip;
        response.price = basePrice * (1.0 - discount);
        response.discount = discount;
        response.validFrom = time(nullptr);

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
        Logger::info(TAG, "Sold %s ticket #%u to Tourist %u",
                     toString(response.ticketType), response.ticketId, request.touristId);
    }

    void sendResponse(const TicketResponse &response, uint32_t touristId) {
        long responseType = CashierMsgType::RESPONSE_BASE + touristId;
        responseQueue_.send(response, responseType);
    }

    SharedMemory<SharedRopewayState> shm_;
    Semaphore sem_;
    MessageQueue<TicketRequest> requestQueue_;
    MessageQueue<TicketResponse> responseQueue_;
    uint32_t nextTicketId_;
};

int main(int argc, char *argv[]) {
    ArgumentParser::CashierArgs args{};
    if (!ArgumentParser::parseCashierArgs(argc, argv, args)) {
        return 1;
    }

    SignalHelper::setup(g_signals, false);
    srand(static_cast<unsigned>(time(nullptr)) ^ static_cast<unsigned>(getpid()));

    try {
        CashierProcess cashier(args);
        cashier.run();
    } catch (const std::exception &e) {
        Logger::error(TAG, "Exception: %s", e.what());
        return 1;
    }

    return 0;
}
