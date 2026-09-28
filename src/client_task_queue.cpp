#include "client_task_queue.h"

bool ClientTaskQueue::push(int client_socket) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            return false;
        }
        sockets_.push(client_socket);
    }
    available_.notify_one();
    return true;
}

bool ClientTaskQueue::pop(int& client_socket) {
    std::unique_lock<std::mutex> lock(mutex_);
    // wait releases the mutex while sleeping and rechecks after every wakeup.
    available_.wait(lock, [this] { return closed_ || !sockets_.empty(); });
    if (sockets_.empty()) {
        return false;
    }
    client_socket = sockets_.front();
    sockets_.pop();
    return true;
}

void ClientTaskQueue::close() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }
    available_.notify_all();
}
