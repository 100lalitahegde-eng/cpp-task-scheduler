#include "scheduler.hpp"
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <iostream>
#include <stdexcept>

int main() {
    std::mutex mutex;
    std::condition_variable cv;
    bool release = false;
    std::atomic<int> count = 0;
    {
        Scheduler scheduler(1);
        auto running = scheduler.submit([&] {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return release; });
            ++count;
        });
        auto cancelled = scheduler.submit([&] { ++count; });
        assert(scheduler.cancel(cancelled.id));
        try { cancelled.result.get(); assert(false); }
        catch (const std::runtime_error&) {}
        { std::lock_guard lock(mutex); release = true; }
        cv.notify_one();
        running.result.get();
        assert(count == 1);
        auto failing = scheduler.submit([] { throw std::runtime_error("task failure"); });
        try { failing.result.get(); assert(false); }
        catch (const std::runtime_error&) {}
    }
    std::cout << "scheduler tests passed\n";
}
