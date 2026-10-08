#include "scheduler.hpp"
#include <atomic>
#include <iostream>

int main() {
    Scheduler scheduler(2, 8);
    std::atomic<unsigned long long> total{0};
    std::vector<Scheduler::Ticket> accepted;
    unsigned rejected = 0;
    // Bounded batch admission: report overload instead of silently growing memory.
    for (unsigned n = 1; n <= 100; ++n) {
        try {
            accepted.push_back(scheduler.submit([&, n] { total.fetch_add(n * n); }));
        } catch (const Scheduler::QueueFull&) { ++rejected; }
    }
    scheduler.shutdown();
    for (auto& ticket : accepted) ticket.result.get();
    const auto stats = scheduler.stats();
    std::cout << "completed=" << stats.completed << " rejected=" << rejected
              << " sum_of_accepted_squares=" << total << '\n';
    return stats.completed + rejected == 100 ? 0 : 1;
}
