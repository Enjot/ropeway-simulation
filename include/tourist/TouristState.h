#pragma once
#include <stdexcept>

/**
 * @brief Tourist state in the simulation.
 *
 * 7-state machine representing the tourist lifecycle.
 */
enum class TouristState {
    BUYING_TICKET, // Arriving and purchasing ticket at the cashier
    WAITING_ENTRY, // Waiting in queue for an entry gate
    WAITING_BOARDING, // On the lower station, waiting for chair assignment
    ON_CHAIR, // Riding on the chairlift
    AT_TOP, // At the upper station
    ON_TRAIL, // Cyclist on downhill trail
    FINISHED // Left the area / simulation complete
};

/**
 * @brief Convert TouristState enum to string representation.
 * @param state TouristState to convert
 * @return String name of the state
 *
 * Tourist states track lifecycle from arrival to departure.
 */
constexpr const char *toString(const TouristState state) {
    switch (state) {
        case TouristState::BUYING_TICKET: return "BUYING_TICKET";
        case TouristState::WAITING_ENTRY: return "WAITING_ENTRY";
        case TouristState::WAITING_BOARDING: return "WAITING_BOARDING";
        case TouristState::ON_CHAIR: return "ON_CHAIR";
        case TouristState::AT_TOP: return "AT_TOP";
        case TouristState::ON_TRAIL: return "ON_TRAIL";
        case TouristState::FINISHED: return "FINISHED";
        default: throw std::invalid_argument("Invalid TouristState value");
    }
}