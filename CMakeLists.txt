cmake_minimum_required(VERSION 3.18)
project(ropeway_simulation C)

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
    src/config.c
    src/logger.c
    src/time_sim.c
    src/ipc.c
)

# Main executable sources
set(MAIN_SOURCES
    src/main.c
    src/cashier.c
    src/lower_worker.c
    src/upper_worker.c
    src/tourist_generator.c
    src/time_server.c
    ${COMMON_SOURCES}
)

# Tourist executable sources
set(TOURIST_SOURCES
    src/tourist_main.c
    ${COMMON_SOURCES}
)

# Main executable
add_executable(ropeway_simulation ${MAIN_SOURCES})
target_link_libraries(ropeway_simulation pthread)

# Tourist executable (separate process, exec'd by generator)
add_executable(tourist ${TOURIST_SOURCES})
target_link_libraries(tourist pthread)

# Copy tourist executable to build directory root for easy access
add_custom_command(TARGET tourist POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:tourist> ${CMAKE_BINARY_DIR}/tourist
)
