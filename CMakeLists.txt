cmake_minimum_required(VERSION 3.16)
project(ropeway_simulation C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE)

# Include directory
include_directories(${CMAKE_SOURCE_DIR}/include)

# Common source files
set(IPC_SOURCES src/ipc_utils.c)

# Main orchestrator
add_executable(main src/main.c ${IPC_SOURCES})

# Cashier process (spawned by main)
add_executable(cashier src/cashier.c ${IPC_SOURCES})

# Tourist process (spawned by main)
add_executable(tourist src/tourist.c ${IPC_SOURCES})
