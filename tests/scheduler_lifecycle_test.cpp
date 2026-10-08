#include "scheduler.hpp"
#include <atomic>
#include <barrier>
#include <iostream>
#include <string>

struct SchedulerTestAccess {
    static void construction_failure() {
        std::atomic<int> live{0};
        int launched = 0;
        try {
            Scheduler scheduler(4, 2, [&](std::function<void()> entry) -> std::thread {
                if (launched++ == 2) throw std::runtime_error("injected thread creation failure");
                return std::thread([&, entry = std::move(entry)] { ++live; entry(); --live; });
            });
            throw std::logic_error("construction unexpectedly succeeded");
        } catch (const std::runtime_error& e) {
            if (std::string(e.what()) != "injected thread creation failure" || live != 0)
                throw std::logic_error("workers not joined on failure");
        }
    }
};
namespace {
void require(bool value) { if (!value) throw std::runtime_error("contract failed"); }
template<class E, class F> void throws(F&& fn) {
    bool caught = false;
    try { fn(); } catch (const E&) { caught = true; }
    require(caught);
}
void capacity_and_cancel() {
    Scheduler s(1, 2);
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    auto running = s.submit([&] { entered.set_value(); gate.wait(); });
    entered.get_future().wait();
    auto first = s.submit([] {});
    auto second = s.submit([] {});
    throws<Scheduler::QueueFull>([&] { s.submit([] {}); });
    require(!s.cancel(running.id));
    require(s.cancel(first.id));
    require(!s.cancel(first.id));
    throws<Scheduler::Cancelled>([&] { first.result.get(); });
    auto replacement = s.submit([] { throw std::runtime_error("task failure"); });
    require(s.stats().queued == 2 && s.stats().active == 1);
    release.set_value();
    s.shutdown();
    running.result.get(); second.result.get();
    throws<std::runtime_error>([&] { replacement.result.get(); });
    throws<Scheduler::Stopped>([&] { s.submit([] {}); });
    auto stats = s.stats();
    require(stats.accepted == 4 && stats.completed == 2 && stats.failed == 1 &&
            stats.cancelled == 1 && stats.rejected == 2 && stats.active == 0 && stats.queued == 0);
}
void shutdown_upgrade() {
    Scheduler s(1, 4);
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    auto active = s.submit([&] { entered.set_value(); gate.wait(); });
    entered.get_future().wait();
    auto pending = s.submit([] { throw std::logic_error("must not run"); });
    s.request_stop();
    throws<Scheduler::Stopped>([&] { s.submit([] {}); });
    std::thread draining([&] { s.shutdown(); });
    s.request_stop(Scheduler::StopMode::cancel_pending);
    throws<Scheduler::Cancelled>([&] { pending.result.get(); });
    std::thread cancelling([&] { s.shutdown(Scheduler::StopMode::cancel_pending); });
    release.set_value();
    draining.join(); cancelling.join(); active.result.get();
    s.shutdown();
    require(s.stats().cancelled == 1);
}
void worker_stop() {
    Scheduler s(1);
    auto ticket = s.submit([&] {
        throws<std::logic_error>([&] { s.shutdown(); });
        s.request_stop();
    });
    ticket.result.get(); s.shutdown();
    throws<Scheduler::Stopped>([&] { s.submit([] {}); });
}
void submit_stop_races() {
    for (int round = 0; round < 30; ++round) {
        Scheduler s(2, 8);
        std::barrier start(5);
        std::atomic<int> executed{0}, cancelled{0};
        std::vector<std::thread> producers;
        for (int p = 0; p < 4; ++p) producers.emplace_back([&] {
            start.arrive_and_wait();
            std::vector<Scheduler::Ticket> accepted;
            for (int i = 0; i < 100; ++i) {
                try { accepted.push_back(s.submit([&] { ++executed; })); }
                catch (const Scheduler::QueueFull&) {}
                catch (const Scheduler::Stopped&) {}
            }
            for (auto& ticket : accepted) {
                try { ticket.result.get(); }
                catch (const Scheduler::Cancelled&) { ++cancelled; }
            }
        });
        start.arrive_and_wait();
        s.shutdown(round % 2 ? Scheduler::StopMode::drain : Scheduler::StopMode::cancel_pending);
        for (auto& producer : producers) producer.join();
        const auto stats = s.stats();
        require(stats.accepted + stats.rejected == 400);
        require(stats.accepted == stats.completed + stats.cancelled);
        require(stats.completed == static_cast<unsigned>(executed) &&
                stats.cancelled == static_cast<unsigned>(cancelled));
        require(stats.queued == 0 && stats.active == 0);
    }
}
}
int main() {
    throws<std::invalid_argument>([] { Scheduler s(0); });
    throws<std::invalid_argument>([] { Scheduler s(1, 0); });
    SchedulerTestAccess::construction_failure();
    capacity_and_cancel(); shutdown_upgrade(); worker_stop(); submit_stop_races();
    std::cout << "scheduler lifecycle tests passed\n";
}
