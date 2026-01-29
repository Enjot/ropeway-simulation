#pragma once

#include <cstdint>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <sstream>
#include "ropeway/gate/GateType.h"
#include "stats/GatePassage.h"

/**
 * @brief Logger for gate passages.
 *
 * Records gate passage events to both shared memory (for real-time access)
 * and optionally to a file (for persistence). Thread-safe via external locking.
 */
class GatePassageLogger {
public:
    static constexpr uint32_t MAX_LOG_ENTRIES = 1000;

    /**
     * Shared memory structure for gate passage logs
     */
    struct GatePassageLog {
        uint32_t count;
        GatePassage entries[MAX_LOG_ENTRIES];

        GatePassageLog() : count{0}, entries{} {
        }
    };

    /**
     * @brief Construct a gate passage logger.
     * @param logFilePath Optional path for file logging (empty = no file)
     */
    explicit GatePassageLogger(const std::string &logFilePath = "")
        : logFilePath_{logFilePath}, logFile_{} {
        if (!logFilePath_.empty()) {
            logFile_.open(logFilePath_, std::ios::out | std::ios::app);
        }
    }

    ~GatePassageLogger() {
        if (logFile_.is_open()) {
            logFile_.close();
        }
    }

    /**
     * @brief Log a gate passage.
     * @param logMem Pointer to shared memory log (can be nullptr)
     * @param passage Gate passage record to log
     *
     * Writes to both shared memory and file if configured.
     */
    void log(GatePassageLog *logMem, const GatePassage &passage) {
        if (logMem != nullptr && logMem->count < MAX_LOG_ENTRIES) {
            logMem->entries[logMem->count] = passage;
            logMem->count++;
        }

        if (logFile_.is_open()) {
            logFile_ << formatPassage(passage) << std::endl;
            logFile_.flush();
        }
    }

    /**
     * @brief Log an entry gate passage.
     * @param logMem Pointer to shared memory log
     * @param touristId Tourist ID
     * @param ticketId Ticket ID
     * @param gateNumber Gate number (0-3)
     * @param wasAllowed Whether passage was allowed
     */
    void logEntry(GatePassageLog *logMem, uint32_t touristId, uint32_t ticketId,
                  uint32_t gateNumber, bool wasAllowed) {
        GatePassage passage;
        passage.touristId = touristId;
        passage.ticketId = ticketId;
        passage.gateType = GateType::ENTRY;
        passage.gateNumber = gateNumber;
        passage.timestamp = time(nullptr);
        passage.wasAllowed = wasAllowed;

        log(logMem, passage);
    }

    /**
     * @brief Log a ride gate passage.
     * @param logMem Pointer to shared memory log
     * @param touristId Tourist ID
     * @param ticketId Ticket ID
     * @param gateNumber Gate number (0-2)
     * @param wasAllowed Whether passage was allowed
     */
    void logRide(GatePassageLog *logMem, uint32_t touristId, uint32_t ticketId,
                 uint32_t gateNumber, bool wasAllowed) {
        GatePassage passage;
        passage.touristId = touristId;
        passage.ticketId = ticketId;
        passage.gateType = GateType::RIDE;
        passage.gateNumber = gateNumber;
        passage.timestamp = time(nullptr);
        passage.wasAllowed = wasAllowed;

        log(logMem, passage);
    }

    /**
     * @brief Statistics calculated from gate passage log.
     */
    struct LogStats {
        uint32_t totalPassages;
        uint32_t entryPassages;
        uint32_t ridePassages;
        uint32_t allowedPassages;
        uint32_t deniedPassages;
    };

    /**
     * @brief Calculate statistics from gate passage log.
     * @param logMem Pointer to shared memory log
     * @return LogStats structure with passage counts
     */
    static LogStats getStats(const GatePassageLog *logMem) {
        LogStats stats{};
        if (logMem == nullptr) return stats;

        stats.totalPassages = logMem->count;
        for (uint32_t i = 0; i < logMem->count; ++i) {
            const auto &entry = logMem->entries[i];
            if (entry.gateType == GateType::ENTRY) {
                stats.entryPassages++;
            } else {
                stats.ridePassages++;
            }
            if (entry.wasAllowed) {
                stats.allowedPassages++;
            } else {
                stats.deniedPassages++;
            }
        }
        return stats;
    }

private:
    std::string formatPassage(const GatePassage &passage) {
        char timeStr[32];
        struct tm *tm_info = localtime(&passage.timestamp);
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", tm_info);

        std::ostringstream oss;
        oss << "[" << timeStr << "] "
                << (passage.gateType == GateType::ENTRY ? "ENTRY" : "RIDE")
                << " Gate " << passage.gateNumber
                << ": Tourist " << passage.touristId
                << " (Ticket " << passage.ticketId << ") - "
                << (passage.wasAllowed ? "ALLOWED" : "DENIED");
        return oss.str();
    }

    std::string logFilePath_;
    std::ofstream logFile_;
};