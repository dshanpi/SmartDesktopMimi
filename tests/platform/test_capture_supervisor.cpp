#include "capture_supervisor.h"
#include <cassert>
#include <chrono>
#include <thread>

template <typename Predicate> static void wait_for(Predicate predicate)
{
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!predicate()) {
        assert(std::chrono::steady_clock::now() < limit);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

int main()
{
    std::atomic<bool> running(true), display(false), stream(false), snapshot(false);
    std::atomic<bool> active(false), stopping(false);
    std::atomic<int> starts(0), stops(0), callbacks(0);
    std::thread supervisor([&] {
        run_capture_supervisor(running, display, stream, snapshot, [&] {
            assert(callbacks.fetch_add(1) == 0);
            assert(!stopping.load());
            if (!active.exchange(true)) ++starts;
            --callbacks;
        }, [&] {
            assert(callbacks.fetch_add(1) == 0);
            if (active.exchange(false)) {
                stopping = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                ++stops;
                stopping = false;
            }
            --callbacks;
        });
    });
    // Idle boot must not acquire capture hardware.
    std::this_thread::sleep_for(std::chrono::milliseconds(160));
    assert(starts == 0);

    display = true;
    wait_for([&] { return starts == 1; });
    stream = true;
    display = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(160));
    assert(active && stops == 0); // shared stream survives DISPLAY 0
    snapshot = true;
    stream = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(160));
    assert(active && stops == 0); // stop_video cannot cancel a snapshot
    snapshot = false;
    wait_for([&] { return stops == 1; });
    assert(!active);

    // A one-shot snapshot must release its capture lease after completion.
    snapshot = true;
    wait_for([&] { return starts == 2; });
    snapshot = false;
    wait_for([&] { return stopping.load(); });
    // Control can accept renewed demand while the single owner joins/releases.
    display = true;
    wait_for([&] { return starts == 3; });
    assert(stops == 2 && active);
    display = false;
    wait_for([&] { return stops == 3; });

    // A disconnected stream clears its demand and returns to idle.
    stream = true;
    wait_for([&] { return starts == 4; });
    stream = false;
    wait_for([&] { return stops == 4; });
    running = false;
    supervisor.join();
    assert(!active && callbacks == 0 && starts == stops);
}
