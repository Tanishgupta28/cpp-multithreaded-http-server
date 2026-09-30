#pragma once
#include <signal.h>

// One instance per process, outliving all workers.
class ShutdownSignal {
public:
    ShutdownSignal();
    ~ShutdownSignal();
    ShutdownSignal(const ShutdownSignal&) = delete;
    ShutdownSignal& operator=(const ShutdownSignal&) = delete;
    int descriptor() const { return pipe_[0]; }
private:
    int pipe_[2];
    struct sigaction previous_{};
};
