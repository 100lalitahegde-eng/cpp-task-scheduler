#include "scheduler.hpp"
#include <atomic>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void concurrent_submit_cancel() {
    constexpr int producers = 4, per_producer = 120;
    auto scheduler = std::make_unique<Scheduler>(4);
    std::vector<std::atomic<int>> calls(producers * per_producer);
    for (auto& count : calls) count.store(0);
    std::atomic<int> cancelled{0};
    std::vector<Scheduler::Ticket> tickets(producers * per_producer);
    std::vector<int> removed(producers * per_producer, 0);
    std::vector<std::thread> threads;
    for (int p = 0; p < producers; ++p) {
        threads.emplace_back([&, p] {
            for (int i = 0; i < per_producer; ++i) {
                const int index = p * per_producer + i;
                const bool fails = index % 17 == 0;
                tickets[index] = scheduler->submit([&, index, fails] {
                    ++calls[index];
                    if (fails) throw std::runtime_error("task failure");
                });
                // A concurrent worker may dequeue before cancellation gets the lock.
                removed[index] = i % 2 == 0 && scheduler->cancel(tickets[index].id);
                if (removed[index]) ++cancelled;
            }
        });
    }
    for (auto& thread : threads) thread.join();
    scheduler.reset(); // Join workers before inspecting exceptions held by futures.
    for (int index = 0; index < producers * per_producer; ++index) {
        const bool fails = index % 17 == 0;
        try {
            tickets[index].result.get();
            require(!removed[index] && !fails, "unexpected successful future");
        } catch (const std::runtime_error& error) {
            const std::string expected = removed[index] ? "cancelled" : "task failure";
            require((removed[index] || fails) && error.what() == expected,
                    "wrong exception from future");
        }
        require(calls[index] == (removed[index] ? 0 : 1), "task ran wrong number of times");
    }
    require(cancelled > 0, "cancellation path not exercised");
}

void shutdown_drains_and_queued_cancel() {
    std::mutex mutex;
    std::condition_variable cv;
    bool release = false;
    std::promise<void> entered;
    std::atomic<int> executed{0};
    auto scheduler = std::make_unique<Scheduler>(1);
    auto running = scheduler->submit([&] {
        entered.set_value();
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return release; });
        ++executed;
    });
    entered.get_future().wait();
    std::vector<Scheduler::Ticket> queued;
    for (int i = 0; i < 80; ++i) queued.push_back(scheduler->submit([&] { ++executed; }));
    std::thread canceller([&] {
        for (std::size_t i = 0; i < queued.size(); i += 2)
            require(scheduler->cancel(queued[i].id), "queued cancellation failed");
    });
    canceller.join();
    std::thread stopper([&] { scheduler.reset(); });
    { std::lock_guard lock(mutex); release = true; }
    cv.notify_one();
    stopper.join();
    running.result.get();
    for (std::size_t i = 0; i < queued.size(); ++i) {
        if (i % 2) queued[i].result.get();
        else {
            try { queued[i].result.get(); require(false, "cancelled future succeeded"); }
            catch (const std::runtime_error& error) {
                require(std::string(error.what()) == "cancelled", "wrong cancellation error");
            }
        }
    }
    require(executed == 41, "shutdown lost or duplicated queued work");
}

void repeated_start_stop() {
    for (int round = 0; round < 100; ++round) {
        std::atomic<int> calls{0};
        std::vector<Scheduler::Ticket> tickets;
        {
            Scheduler scheduler(2);
            for (int i = 0; i < 12; ++i)
                tickets.push_back(scheduler.submit([&] { ++calls; }));
        }
        for (auto& ticket : tickets) ticket.result.get();
        require(calls == 12, "start/stop lost work");
    }
}
} // namespace

int main() {
    concurrent_submit_cancel();
    shutdown_drains_and_queued_cancel();
    repeated_start_stop();
    std::cout << "scheduler stress tests passed\n";
}
