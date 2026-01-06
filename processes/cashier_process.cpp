#include <iostream>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <ctime>

#include "ipc/SharedMemory.hpp"
#include "ipc/Semaphore.hpp"
#include "ipc/MessageQueue.hpp"
#include "ipc/ropeway_system_state.hpp"
#include "ipc/semaphore_index.hpp"
#include "ipc/cashier_message.hpp"
#include "common/config.hpp"

namespace {
    volatile sig_atomic_t g_shouldExit = 0;

    void signalHandler(int signum) {
        if (signum == SIGTERM || signum == SIGINT) {
            g_shouldExit = 1;
        }
    }

    void setupSignalHandlers() {
        struct sigaction sa{};
        sa.sa_handler = signalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        if (sigaction(SIGTERM, &sa, nullptr) == -1) {
            perror("sigaction SIGTERM");
        }
        if (sigaction(SIGINT, &sa, nullptr) == -1) {
            perror("sigaction SIGINT");
        }
    }

    void printUsage(const char* programName) {
        std::cerr << "Usage: " << programName << " <shmKey> <semKey> <cashierMsgKey>\n";
    }

    struct CashierArgs {
        key_t shmKey;
        key_t semKey;
        key_t cashierMsgKey;
    };

    bool parseArgs(int argc, char* argv[], CashierArgs& args) {
        if (argc != 4) {
            printUsage(argv[0]);
            return false;
        }

        char* endPtr = nullptr;

        args.shmKey = static_cast<key_t>(std::strtol(argv[1], &endPtr, 10));
        if (*endPtr != '\0') {
            std::cerr << "Error: Invalid shmKey\n";
            return false;
        }

        args.semKey = static_cast<key_t>(std::strtol(argv[2], &endPtr, 10));
        if (*endPtr != '\0') {
            std::cerr << "Error: Invalid semKey\n";
            return false;
        }

        args.cashierMsgKey = static_cast<key_t>(std::strtol(argv[3], &endPtr, 10));
        if (*endPtr != '\0') {
            std::cerr << "Error: Invalid cashierMsgKey\n";
            return false;
        }

        return true;
    }

    const char* ticketTypeToString(TicketType type) {
        switch (type) {
            case TicketType::SINGLE_USE: return "SINGLE_USE";
            case TicketType::TIME_TK1: return "TIME_TK1 (1h)";
            case TicketType::TIME_TK2: return "TIME_TK2 (2h)";
            case TicketType::TIME_TK3: return "TIME_TK3 (4h)";
            case TicketType::DAILY: return "DAILY";
            default: return "UNKNOWN";
        }
    }
}

class CashierProcess {
public:
    CashierProcess(const CashierArgs& args)
        : shm_{args.shmKey, false},
          sem_{args.semKey, SemaphoreIndex::TOTAL_SEMAPHORES, false},
          requestQueue_{args.cashierMsgKey, false},
          responseQueue_{args.cashierMsgKey, false},
          nextTicketId_{1},
          totalRevenue_{0.0},
          ticketsSold_{0},
          vipTickets_{0},
          discountsGiven_{0} {

        std::cout << "[Cashier] Started (PID: " << getpid() << ")" << std::endl;
    }

    void run() {
        std::cout << "[Cashier] Ready to serve customers" << std::endl;
        std::cout << "[Cashier] Prices: Single=" << TicketPricing::SINGLE_USE
                  << ", Tk1=" << TicketPricing::TIME_TK1
                  << ", Tk2=" << TicketPricing::TIME_TK2
                  << ", Tk3=" << TicketPricing::TIME_TK3
                  << ", Daily=" << TicketPricing::DAILY << std::endl;

        while (!g_shouldExit) {
            // Check if ropeway is still accepting tourists
            bool accepting;
            {
                SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                accepting = shm_->acceptingNewTourists;
            }

            if (!accepting) {
                // Still process remaining requests but inform them
            }

            // Try to receive a ticket request (non-blocking with short timeout)
            auto request = requestQueue_.tryReceive(CashierMsgType::REQUEST);
            if (request) {
                processRequest(*request, accepting);
            }

            usleep(10000); // 10ms cycle - fast response time
        }

        printStatistics();
        std::cout << "[Cashier] Shutting down" << std::endl;
    }

private:
    void processRequest(const TicketRequest& request, bool accepting) {
        std::cout << "[Cashier] Processing request from Tourist " << request.touristId
                  << " (age: " << request.touristAge << ", type: "
                  << ticketTypeToString(request.requestedType) << ")" << std::endl;

        TicketResponse response;
        response.mtype = CashierMsgType::RESPONSE_BASE + request.touristId;
        response.touristId = request.touristId;

        if (!accepting) {
            response.success = false;
            std::strncpy(response.message, "Ropeway is closed", sizeof(response.message) - 1);
            sendResponse(response);
            std::cout << "[Cashier] Rejected: Ropeway closed" << std::endl;
            return;
        }

        // Calculate price and discount
        double basePrice = TicketPricing::getPrice(request.requestedType);
        double discount = 0.0;

        // Apply discounts
        if (request.touristAge < Config::Discount::CHILD_DISCOUNT_AGE) {
            discount = Config::Discount::CHILD_DISCOUNT;
            discountsGiven_++;
        } else if (request.touristAge >= Config::Age::SENIOR_AGE_FROM) {
            discount = Config::Discount::SENIOR_DISCOUNT;
            discountsGiven_++;
        }

        double finalPrice = basePrice * (1.0 - discount);

        // Determine VIP status
        // In production: 1% random chance OR explicit VIP membership
        // For testing: if requested, grant VIP status
        bool isVip = false;
        if (request.requestVip) {
            // Grant VIP if explicitly requested (simulates VIP membership)
            isVip = true;
            vipTickets_++;
        } else {
            // Random 1% chance for non-requesting tourists (lucky VIP)
            isVip = (rand() % 100) < static_cast<int>(Config::Vip::VIP_CHANCE_PERCENTAGE * 100);
            if (isVip) {
                vipTickets_++;
            }
        }

        // Generate ticket
        uint32_t ticketId = nextTicketId_++;
        time_t now = time(nullptr);
        time_t validUntil;

        switch (request.requestedType) {
            case TicketType::SINGLE_USE:
                validUntil = now + 24 * 3600; // Valid for 24 hours
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
                // Valid until closing time
                {
                    SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
                    validUntil = shm_->closingTime;
                }
                break;
        }

        // Prepare response
        response.success = true;
        response.ticketId = ticketId;
        response.ticketType = request.requestedType;
        response.isVip = isVip;
        response.price = finalPrice;
        response.discount = discount;
        response.validFrom = now;
        response.validUntil = validUntil;

        char discountStr[32] = "";
        if (discount > 0) {
            snprintf(discountStr, sizeof(discountStr), " (%.0f%% discount)", discount * 100);
        }
        snprintf(response.message, sizeof(response.message),
                 "Ticket #%u issued%s%s",
                 ticketId,
                 discountStr,
                 isVip ? " [VIP]" : "");

        sendResponse(response);

        // Update statistics
        totalRevenue_ += finalPrice;
        ticketsSold_++;

        std::cout << "[Cashier] Sold ticket #" << ticketId << " to Tourist " << request.touristId
                  << " - " << ticketTypeToString(request.requestedType)
                  << " - Price: " << finalPrice;
        if (discount > 0) {
            std::cout << " (discount: " << (discount * 100) << "%)";
        }
        if (isVip) {
            std::cout << " [VIP]";
        }
        std::cout << std::endl;
    }

    void sendResponse(const TicketResponse& response) {
        if (!responseQueue_.send(response)) {
            std::cerr << "[Cashier] Failed to send response to Tourist " << response.touristId << std::endl;
        }
    }

    void printStatistics() {
        std::cout << "\n[Cashier] === Sales Statistics ===" << std::endl;
        std::cout << "[Cashier] Tickets sold: " << ticketsSold_ << std::endl;
        std::cout << "[Cashier] VIP tickets: " << vipTickets_ << std::endl;
        std::cout << "[Cashier] Discounts given: " << discountsGiven_ << std::endl;
        std::cout << "[Cashier] Total revenue: " << totalRevenue_ << std::endl;

        // Save revenue to shared stats
        {
            SemaphoreLock lock(sem_, SemaphoreIndex::SHARED_MEMORY);
            shm_->dailyStats.totalRevenueWithDiscounts = totalRevenue_;
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
    CashierArgs args{};
    if (!parseArgs(argc, argv, args)) {
        return 1;
    }

    setupSignalHandlers();
    srand(static_cast<unsigned>(time(nullptr)) ^ static_cast<unsigned>(getpid()));

    try {
        CashierProcess cashier(args);
        cashier.run();
    } catch (const std::exception& e) {
        std::cerr << "[Cashier] Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
