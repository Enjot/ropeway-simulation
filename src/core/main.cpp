#include "core/Simulation.h"
#include "core/Config.h"
#include <iostream>

int main() {
    try {
        Config::loadEnvFile();
        Config::validate();
    } catch (const std::exception& e) {
        std::cerr << "Config error: " << e.what() << "\n";
        return 1;
    }

    Simulation simulation;
    simulation.run();
    return 0;
}
