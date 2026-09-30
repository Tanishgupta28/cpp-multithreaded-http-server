#include "shutdown_signal.h"
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <system_error>

namespace {
volatile sig_atomic_t notification_fd = -1;
void notify_shutdown(int) {
    const int saved_errno = errno;
    const char byte = 1;
    // write is async-signal-safe. A full pipe already contains a notification.
    ssize_t result;
    do {
        result = write(notification_fd, &byte, 1);
    } while (result == -1 && errno == EINTR);
    errno = saved_errno;
}
}

ShutdownSignal::ShutdownSignal() {
    if (pipe2(pipe_, O_NONBLOCK | O_CLOEXEC) == -1)
        throw std::system_error(errno, std::generic_category(), "shutdown pipe");
    notification_fd = pipe_[1];
    struct sigaction action{};
    action.sa_handler = notify_shutdown;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, &previous_) == -1) {
        const int error = errno;
        notification_fd = -1;
        close(pipe_[0]);
        close(pipe_[1]);
        throw std::system_error(error, std::generic_category(), "sigaction");
    }
}

ShutdownSignal::~ShutdownSignal() {
    // Workers have joined, so no worker can still be executing the handler.
    sigaction(SIGINT, &previous_, nullptr);
    notification_fd = -1;
    close(pipe_[0]);
    close(pipe_[1]);
}
