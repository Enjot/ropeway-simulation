#include <cstring>
#include <unistd.h>
#include <ctime>
#include <cstdlib>

#include "ipc/core/SharedMemory.hpp"
#include "ipc/core/Semaphore.hpp"
#include "ipc/core/MessageQueue.hpp"
#include "ipc/RopewaySystemState.hpp"
#include "ipc/message/CashierMessage.hpp"
#include "Config.hpp"
#include "utils/SignalHelper.hpp"
#include "utils/ArgumentParser.hpp"
#include "utils/Logger.hpp"

namespace {
    SignalHelper::Flags g_signals;
    constexpr const char* TAG = "Cashier";
}

class CashierProcess {
public:
    CashierProcess(const ArgumentParser::CashierArgs& args)
        : shm_{SharedMemory<RopewaySystemState>::attach(args.shmKey)},
          sem_{args.semKey},
          requestQueue_{args.cashierMsgKey, "CashierReq"},
          responseQueue_{args.cashierMsgKey, "CashierResp"},
          nextTicketId_{1} {

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

        Logger::info(TAG, "Shutting down");
    }

private:
    void processRequest(const TicketRequest& request) {
        Logger::info(TAG, "Processing Tourist %u (age %u)", request.touristId, request.touristAge);

        TicketResponse response;
        response.touristId = request.touristId;

        // Check if accepting
        bool accepting;
        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
            accepting = shm_->core.acceptingNewTourists;
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

        if (request.touristAge < Config::Discount::CHILD_DISCOUNT_AGE) {
            discount = Config::Discount::CHILD_DISCOUNT;
        } else if (request.touristAge >= Config::Age::SENIOR_AGE_FROM) {
            discount = Config::Discount::SENIOR_DISCOUNT;
        }

        // Issue ticket
        response.success = true;
        response.ticketId = nextTicketId_++;
        response.ticketType = request.requestedType;
        response.isVip = request.requestVip;
        response.price = basePrice * (1.0 - discount);
        response.discount = discount;
        response.validFrom = time(nullptr);
        response.validUntil = response.validFrom + 24 * 3600;
        strcpy(response.message, "Ticket issued");

        sendResponse(response, request.touristId);
        Logger::info(TAG, "Sold ticket #%u to Tourist %u", response.ticketId, request.touristId);
    }

    void sendResponse(const TicketResponse& response, uint32_t touristId) {
        long responseType = CashierMsgType::RESPONSE_BASE + touristId;
        responseQueue_.send(response, responseType);
    }

    SharedMemory<RopewaySystemState> shm_;
    Semaphore sem_;
    MessageQueue<TicketRequest> requestQueue_;
    MessageQueue<TicketResponse> responseQueue_;
    uint32_t nextTicketId_;
};

int main(int argc, char* argv[]) {
    ArgumentParser::CashierArgs args{};
    if (!ArgumentParser::parseCashierArgs(argc, argv, args)) {
        return 1;
    }

    SignalHelper::setup(g_signals, false);
    srand(static_cast<unsigned>(time(nullptr)) ^ static_cast<unsigned>(getpid()));

    try {
        CashierProcess cashier(args);
        cashier.run();
    } catch (const std::exception& e) {
        Logger::error(TAG, "Exception: %s", e.what());
        return 1;
    }

    return 0;
}
