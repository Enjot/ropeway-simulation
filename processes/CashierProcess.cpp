#include <cstring>
#include <unistd.h>
#include <ctime>
#include <cstdlib>

#include "ipc/SharedMemory.hpp"
#include "../ipc/core/Semaphore.hpp"
#include "../ipc/core/MessageQueue.hpp"
#include "ipc/RopewaySystemState.hpp"
#include "../ipc/core/Semaphore.hpp"
#include "../ipc/message/CashierMessage.hpp"
#include "../Config.hpp"
#include "utils/SignalHelper.hpp"
#include "utils/ArgumentParser.hpp"
#include "utils/Logger.hpp"

namespace {
    SignalHelper::SignalFlags g_signals;
    constexpr const char* TAG = "Cashier";
}

class CashierProcess {
public:
    CashierProcess(const ArgumentParser::CashierArgs& args)
        : shm_{args.shmKey, false},
          sem_{args.semKey},
          requestQueue_{args.cashierMsgKey, false},
          responseQueue_{args.cashierMsgKey, false},
          nextTicketId_{1},
          totalRevenue_{0.0},
          ticketsSold_{0},
          vipTickets_{0},
          discountsGiven_{0} {

        Logger::info(TAG, "Started (PID: ", getpid(), ")");

        // Signal readiness to parent process
        sem_.post(Semaphore::Index::CASHIER_READY, false);
    }

    void run() {
        Logger::info(TAG, "Ready to serve customers");
        Logger::info(TAG, "Prices configured");

        while (!SignalHelper::shouldExit(g_signals)) {
            // Use blocking receive - will be interrupted by signals (SIGTERM)
            auto request = requestQueue_.receive(CashierMsgType::REQUEST);
            if (!request) {
                // Interrupted by signal or error
                continue;
            }

            bool accepting;
            {
                Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
                accepting = shm_->core.acceptingNewTourists;
            }

            processRequest(*request, accepting);
        }

        printStatistics();
        Logger::info(TAG, "Shutting down");
    }

private:
    void processRequest(const TicketRequest& request, bool accepting) {
        Logger::info(TAG, "Processing request from Tourist ", request.touristId,
                    " (age: ", request.touristAge, ")");

        TicketResponse response;
        response.mtype = CashierMsgType::RESPONSE_BASE + request.touristId;
        response.touristId = request.touristId;

        if (!accepting) {
            response.success = false;
            std::strncpy(response.message, "Ropeway is closed", sizeof(response.message) - 1);
            sendResponse(response);
            Logger::info(TAG, "Rejected: Ropeway closed");
            return;
        }

        double basePrice = TicketPricing::getPrice(request.requestedType);
        double discount = 0.0;

        if (request.touristAge < Config::Discount::CHILD_DISCOUNT_AGE) {
            discount = Config::Discount::CHILD_DISCOUNT;
            discountsGiven_++;
        } else if (request.touristAge >= Config::Age::SENIOR_AGE_FROM) {
            discount = Config::Discount::SENIOR_DISCOUNT;
            discountsGiven_++;
        }

        double finalPrice = basePrice * (1.0 - discount);

        bool isVip = false;
        if (request.requestVip) {
            isVip = true;
            vipTickets_++;
        } else {
            isVip = (rand() % 100) < static_cast<int>(Config::Vip::VIP_CHANCE_PERCENTAGE * 100);
            if (isVip) {
                vipTickets_++;
            }
        }

        uint32_t ticketId = nextTicketId_++;
        time_t now = time(nullptr);
        time_t validUntil;

        switch (request.requestedType) {
            case TicketType::SINGLE_USE:
                validUntil = now + 24 * 3600;
                break;
            case TicketType::TIME_TK1:
                validUntil = now + TicketPricing::TK1_DURATION;
                break;
            case TicketType::TIME_TK2:
                validUntil = now + TicketPricing::TK2_DURATION;
                break;
            case TicketType::TIME_TK3:
                validUntil = now + TicketPricing::TK3_DURATION;
                break;
            case TicketType::DAILY:
                {
                    Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
                    validUntil = shm_->core.closingTime;
                }
                break;
        }

        response.success = true;
        response.ticketId = ticketId;
        response.ticketType = request.requestedType;
        response.isVip = isVip;
        response.price = finalPrice;
        response.discount = discount;
        response.validFrom = now;
        response.validUntil = validUntil;

        strcpy(response.message, "Ticket issued");
        if (isVip) {
            strcat(response.message, " [VIP]");
        }

        sendResponse(response);

        totalRevenue_ += finalPrice;
        ticketsSold_++;

        if (isVip) {
            Logger::info(TAG, "Sold ticket #", ticketId, " to Tourist ", request.touristId, " [VIP]");
        } else {
            Logger::info(TAG, "Sold ticket #", ticketId, " to Tourist ", request.touristId);
        }
    }

    void sendResponse(const TicketResponse& response) {
        if (!responseQueue_.send(response)) {
            Logger::perror(TAG, "msgsnd response");
        }
    }

    void printStatistics() {
        Logger::info(TAG, "=== Sales Statistics ===");
        Logger::info(TAG, "Tickets sold: ", ticketsSold_);
        Logger::info(TAG, "VIP tickets: ", vipTickets_);
        Logger::info(TAG, "Discounts given: ", discountsGiven_);
        Logger::info(TAG, "Total revenue: ", static_cast<unsigned int>(totalRevenue_));

        {
            Semaphore::ScopedLock lock(sem_, Semaphore::Index::SHARED_MEMORY);
            shm_->stats.dailyStats.totalRevenueWithDiscounts = totalRevenue_;
        }
    }

    SharedMemory<RopewaySystemState> shm_;
    Semaphore sem_;
    MessageQueue<TicketRequest> requestQueue_;
    MessageQueue<TicketResponse> responseQueue_;

    uint32_t nextTicketId_;
    double totalRevenue_;
    uint32_t ticketsSold_;
    uint32_t vipTickets_;
    uint32_t discountsGiven_;
};

int main(int argc, char* argv[]) {
    ArgumentParser::CashierArgs args{};
    if (!ArgumentParser::parseCashierArgs(argc, argv, args)) {
        return 1;
    }

    SignalHelper::setup(g_signals, SignalHelper::Mode::BASIC);
    srand(static_cast<unsigned>(time(nullptr)) ^ static_cast<unsigned>(getpid()));

    try {
        CashierProcess cashier(args);
        cashier.run();
    } catch (const std::exception& e) {
        Logger::perror(TAG, e.what());
        return 1;
    }

    return 0;
}
