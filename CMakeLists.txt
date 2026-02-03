cmake_minimum_required(VERSION 3.18)
project(ropeway_simulation C)

# Generate compile_commands.json for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Enable POSIX and System V IPC extensions (required for sigaction, key_t, etc.)
add_definitions(-D_GNU_SOURCE)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2")

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Common source files (shared between main and tourist)
set(COMMON_SOURCES
    src/core/config.c
    src/core/logger.c
    src/core/time_sim.c
    src/ipc/ipc.c
    src/ipc/keys.c
    src/ipc/sem.c
    src/ipc/mq.c
    src/ipc/shm.c
    src/ipc/sync.c
)

# Main executable sources
set(MAIN_SOURCES
    src/main.c
    src/core/report.c
    src/lifecycle/process_signals.c
    src/lifecycle/process_manager.c
    src/lifecycle/zombie_reaper.c
    src/processes/cashier.c
    src/processes/lower_worker.c
    src/processes/upper_worker.c
    src/common/worker_emergency.c
    src/processes/tourist_generator.c
    src/processes/time_server.c
    ${COMMON_SOURCES}
)

# Tourist executable sources
set(TOURIST_SOURCES
    src/tourist/main.c
    src/tourist/init.c
    src/tourist/threads.c
    src/tourist/lifecycle.c
    src/tourist/boarding.c
    src/tourist/movement.c
    src/tourist/stats.c
    ${COMMON_SOURCES}
)

# Main executable
add_executable(ropeway_simulation ${MAIN_SOURCES})
target_link_libraries(ropeway_simulation pthread)

# Define tourist executable path for the main simulation
target_compile_definitions(ropeway_simulation PRIVATE
    TOURIST_EXE_PATH="${CMAKE_BINARY_DIR}/tourist"
)

# Tourist executable (separate process, exec'd by generator)
add_executable(tourist ${TOURIST_SOURCES})
target_link_libraries(tourist pthread)

# Copy tourist executable to build directory root for easy access
add_custom_command(TARGET tourist POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:tourist> ${CMAKE_BINARY_DIR}/tourist
)
