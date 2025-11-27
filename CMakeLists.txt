cmake_minimum_required(VERSION 4.0)
project(ropeway_simulation)

set(CMAKE_CXX_STANDARD 26)

# Add include directory for header files
include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(ropeway_simulation main.cpp
        include/ipc/ropeway_system_state.hpp
        include/common/config.hpp
        include/common/pass_type.hpp
        include/common/ropeway_state.hpp
        include/common/station_type.hpp
        include/common/tourist_type.hpp
        include/common/trail_difficulty.hpp
        include/common/worker_signal.hpp
        include/ipc/semaphore_index.hpp
        include/ipc/worker_message.hpp
        include/structures/chair.hpp
        include/structures/daily_statistic.hpp
        include/structures/gate_passage.hpp
        include/structures/pass.hpp
)
