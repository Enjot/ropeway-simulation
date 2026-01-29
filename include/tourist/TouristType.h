#pragma once

/**
 * @brief Type of tourist using the ropeway
 */
enum class TouristType {
    PEDESTRIAN,
    CYCLIST
};

/**
 * @breif Convert TouristType enum to string representation.
 * @param type TouristType to convert
 */
constexpr const char *toString(const TouristType type) {
    switch (type) {
        case TouristType::PEDESTRIAN: return "PEDESTRIAN";
        case TouristType::CYCLIST: return "CYCLIST";
        default: throw std::invalid_argument("Invalid TouristType value");
    }
}