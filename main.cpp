#include "processes/Orchestrator.hpp"

int main() {
    try {
        Orchestrator orchestrator;
        orchestrator.run();
    } catch (const std::exception& e) {
        Logger::perr("Main", e.what());
        return 1;
    }

    return 0;
}
