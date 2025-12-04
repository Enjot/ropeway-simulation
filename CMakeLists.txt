cmake_minimum_required(VERSION 4.0)
project(ropeway_simulation)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(ropeway_simulation main.cpp
        # Common headers
        include/common/config.hpp
        include/common/gate_type.hpp
        include/common/ropeway_state.hpp
        include/common/station_type.hpp
        include/common/ticket_type.hpp
        include/common/tourist_type.hpp
        include/common/trail_difficulty.hpp
        include/common/worker_signal.hpp
        # IPC headers
        include/ipc/MessageQueue.hpp
        include/ipc/ropeway_system_state.hpp
        include/ipc/semaphore_index.hpp
        include/ipc/Semaphore.hpp
        include/ipc/SharedMemory.hpp
        include/ipc/worker_message.hpp
        # Structure headers
        include/structures/chair.hpp
        include/structures/daily_statistic.hpp
        include/structures/gate_passage.hpp
        include/structures/ticket.hpp
)
