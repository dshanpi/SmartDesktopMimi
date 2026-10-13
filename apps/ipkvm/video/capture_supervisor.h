#ifndef MIMI_CAPTURE_SUPERVISOR_H
#define MIMI_CAPTURE_SUPERVISOR_H

#include <atomic>
#include <unistd.h>

// One owner invokes both lifecycle callbacks. Consumers only publish demand,
// so a slow V4L2 release never holds up control replies or snapshot requests.
template <typename Start, typename Stop>
void run_capture_supervisor(const std::atomic<bool>& running,
                            const std::atomic<bool>& display,
                            const std::atomic<bool>& stream,
                            const std::atomic<bool>& snapshot,
                            Start start, Stop stop)
{
    while (running.load()) {
        if (display.load() || stream.load() || snapshot.load()) {
            start();
        } else {
            stop();
        }
        usleep(50000);
    }
}

#endif
