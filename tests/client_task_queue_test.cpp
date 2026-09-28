#include "client_task_queue.h"

#include <chrono>
#include <future>
#include <stdexcept>

void require(bool condition) {
    if (!condition) {
        throw std::runtime_error("ClientTaskQueue check failed");
    }
}

int main() {
    using namespace std::chrono_literals;
    ClientTaskQueue queue;
    // Integer tokens exercise ordering without opening real sockets.
    require(queue.push(10));
    require(queue.push(20));
    int value;
    require(queue.pop(value) && value == 10);
    require(queue.pop(value) && value == 20);

    auto waiting = std::async(std::launch::async, [&queue] {
        int socket;
        return queue.pop(socket) ? socket : -1;
    });
    require(waiting.wait_for(50ms) == std::future_status::timeout);
    require(queue.push(30));
    require(waiting.wait_for(2s) == std::future_status::ready);
    require(waiting.get() == 30);

    require(queue.push(40));
    queue.close();
    queue.close();
    require(!queue.push(50));
    require(queue.pop(value) && value == 40);
    require(!queue.pop(value));

    ClientTaskQueue empty;
    auto stopping = std::async(std::launch::async, [&empty] {
        int socket;
        return empty.pop(socket);
    });
    require(stopping.wait_for(50ms) == std::future_status::timeout);
    empty.close();
    require(stopping.wait_for(2s) == std::future_status::ready);
    require(!stopping.get());
}
