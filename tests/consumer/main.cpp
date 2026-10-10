#include <scheduler.hpp>
#include <atomic>
#include <stdexcept>

int main() {
    std::atomic<int> completed{0};
    Scheduler scheduler(2, 4);
    auto success = scheduler.submit([&] { ++completed; });
    auto failure = scheduler.submit([] { throw std::runtime_error("consumer failure"); });
    success.result.get();
    bool propagated = false;
    try { failure.result.get(); }
    catch (const std::runtime_error&) { propagated = true; }
    scheduler.shutdown();
    const auto stats = scheduler.stats();
    return completed == 1 && propagated && stats.completed == 1 &&
           stats.failed == 1 && stats.active == 0 && stats.queued == 0 ? 0 : 1;
}
