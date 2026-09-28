#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

class ClientTaskQueue {
public:
    // Ownership transfers only when push succeeds; pop transfers it to a worker.
    bool push(int client_socket);
    bool pop(int& client_socket);
    // Wake consumers; queued clients are drained before pop returns false.
    void close();

private:
    std::queue<int> sockets_;
    std::mutex mutex_;
    std::condition_variable available_;
    bool closed_ = false;
};
