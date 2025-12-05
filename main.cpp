#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

#include "ipc/SharedMemory.hpp"
#include "ipc/Semaphore.hpp"
#include "ipc/MessageQueue.hpp"
#include "ipc/ropeway_system_state.hpp"
#include "ipc/worker_message.hpp"
#include "ipc/semaphore_index.hpp"
#include "common/config.hpp"

void testSharedMemory() {
    std::cout << "=== Testing SharedMemory ===" << std::endl;

    constexpr key_t SHM_KEY = Config::Ipc::SHM_KEY_BASE;

    SharedMemory<RopewaySystemState>::removeByKey(SHM_KEY);

    {
        SharedMemory<RopewaySystemState> shm(SHM_KEY, true);
        std::cout << "Created shared memory with ID: " << shm.getId() << std::endl;

        shm->state = RopewayState::RUNNING;
        shm->touristsInLowerStation = 10;

        std::cout << "Set state to RUNNING, tourists: " << shm->touristsInLowerStation << std::endl;

        pid_t pid = fork();
        if (pid == 0) {
            SharedMemory<RopewaySystemState> childShm(SHM_KEY, false);
            std::cout << "[Child] Read tourists: " << childShm->touristsInLowerStation << std::endl;
            childShm->touristsInLowerStation = 20;
            std::cout << "[Child] Updated tourists to 20" << std::endl;
            _exit(0);
        } else if (pid > 0) {
            waitpid(pid, nullptr, 0);
            std::cout << "[Parent] Tourists after child update: " << shm->touristsInLowerStation << std::endl;
        } else {
            perror("fork");
        }
    }

    std::cout << "SharedMemory test completed (memory cleaned up)" << std::endl;
}

void testSemaphore() {
    std::cout << "\n=== Testing Semaphore ===" << std::endl;

    constexpr key_t SEM_KEY = Config::Ipc::SEM_KEY_BASE;

    Semaphore::removeByKey(SEM_KEY);

    {
        Semaphore sem(SEM_KEY, SemaphoreIndex::TOTAL_SEMAPHORES, true);
        std::cout << "Created semaphore set with ID: " << sem.getId() << std::endl;

        sem.setValue(SemaphoreIndex::STATION_CAPACITY, static_cast<int>(Config::Gate::MAX_TOURISTS_ON_STATION));
        std::cout << "Station capacity semaphore set to: " << sem.getValue(SemaphoreIndex::STATION_CAPACITY) << std::endl;

        sem.setValue(SemaphoreIndex::SHARED_MEMORY, 1);
        std::cout << "Shared memory mutex initialized to: " << sem.getValue(SemaphoreIndex::SHARED_MEMORY) << std::endl;

        {
            SemaphoreLock lock(sem, SemaphoreIndex::SHARED_MEMORY);
            std::cout << "Acquired lock, semaphore value: " << sem.getValue(SemaphoreIndex::SHARED_MEMORY) << std::endl;
        }
        std::cout << "Released lock, semaphore value: " << sem.getValue(SemaphoreIndex::SHARED_MEMORY) << std::endl;

        sem.wait(SemaphoreIndex::STATION_CAPACITY);
        std::cout << "After one tourist entered, capacity: " << sem.getValue(SemaphoreIndex::STATION_CAPACITY) << std::endl;

        sem.signal(SemaphoreIndex::STATION_CAPACITY);
        std::cout << "After tourist left, capacity: " << sem.getValue(SemaphoreIndex::STATION_CAPACITY) << std::endl;
    }

    std::cout << "Semaphore test completed (semaphores cleaned up)" << std::endl;
}

void testMessageQueue() {
    std::cout << "\n=== Testing MessageQueue ===" << std::endl;

    constexpr key_t MSG_KEY = Config::Ipc::MSG_KEY_BASE;

    MessageQueue<WorkerMessage>::removeByKey(MSG_KEY);

    {
        MessageQueue<WorkerMessage> msgQueue(MSG_KEY, true);
        std::cout << "Created message queue with ID: " << msgQueue.getId() << std::endl;

        WorkerMessage msg;
        msg.mtype = 1;
        msg.senderId = 1;
        msg.receiverId = 2;
        msg.signal = WorkerSignal::EMERGENCY_STOP;
        msg.timestamp = time(nullptr);
        std::strncpy(msg.messageText, "Emergency stop!", sizeof(msg.messageText) - 1);

        if (msgQueue.send(msg)) {
            std::cout << "Sent message from Worker 1 to Worker 2" << std::endl;
        }

        std::cout << "Messages in queue: " << msgQueue.getMessageCount() << std::endl;

        auto received = msgQueue.receive(1);
        if (received) {
            std::cout << "Received message:" << std::endl;
            std::cout << "  From: Worker " << received->senderId << std::endl;
            std::cout << "  To: Worker " << received->receiverId << std::endl;
            std::cout << "  Signal: " << static_cast<int>(received->signal) << std::endl;
            std::cout << "  Text: " << received->messageText << std::endl;
        }

        pid_t pid = fork();
        if (pid == 0) {
            MessageQueue<WorkerMessage> childQueue(MSG_KEY, false);

            WorkerMessage response;
            response.mtype = 2;
            response.senderId = 2;
            response.receiverId = 1;
            response.signal = WorkerSignal::READY_TO_START;
            response.timestamp = time(nullptr);
            std::strncpy(response.messageText, "Ready to resume", sizeof(response.messageText) - 1);

            childQueue.send(response);
            std::cout << "[Child] Sent response message" << std::endl;
            _exit(0);
        } else if (pid > 0) {
            waitpid(pid, nullptr, 0);

            auto response = msgQueue.receive(2);
            if (response) {
                std::cout << "[Parent] Received response: " << response->messageText << std::endl;
            }
        } else {
            perror("fork");
        }
    }

    std::cout << "MessageQueue test completed (queue cleaned up)" << std::endl;
}

int main() {
    std::cout << "Ropeway Simulation - IPC Wrappers Test\n" << std::endl;

    testSharedMemory();
    testSemaphore();
    testMessageQueue();

    std::cout << "\n=== All tests completed ===" << std::endl;

    return 0;
}
