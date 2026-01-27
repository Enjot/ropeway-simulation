#include "Simulation.hpp"
#include "core/Config.hpp"
#include <iostream>

int main() {
    try {
        Config::validate();
    } catch (const std::exception& e) {
        std::cerr << "Config error: " << e.what() << "\n";
        std::cerr << "Run: source ropeway.env && ./main\n";
        return 1;
    }

    Simulation simulation;
    simulation.run();
    return 0;
}
