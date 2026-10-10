#include "scheduler.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

using Clock = std::chrono::steady_clock;
using Micros = std::chrono::duration<double, std::micro>;

namespace {
std::size_t number(const char* text, std::size_t maximum) {
    std::size_t value = 0;
    const std::string_view input(text);
    const auto result = std::from_chars(input.data(), input.data() + input.size(), value);
    if (result.ec != std::errc{} || result.ptr != input.data() + input.size() || value > maximum)
        throw std::invalid_argument("invalid numeric argument");
    return value;
}
// Current resident pages, not process high-water RSS. Linux only; -1 elsewhere.
long rss_kib() {
#ifdef __linux__
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream value(line.substr(6));
            long kib = -1;
            value >> kib;
            return kib;
        }
    }
#endif
    return -1;
}
double percentile(std::vector<double> values, double quantile) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(std::ceil(quantile * values.size())) - 1;
    return values[index];
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Sample { double queue_us = 0, total_us = 0; bool finished = false; };
}

int main(int argc, char** argv) {
    try {
        if (argc != 7) throw std::invalid_argument(
            "usage: scheduler_benchmark cpu|blocking|overload workers capacity attempts service_us payload_bytes");
        const std::string mode(argv[1]);
        require(mode == "cpu" || mode == "blocking" || mode == "overload", "unknown workload");
        const auto workers = number(argv[2], 256);
        const auto capacity = number(argv[3], 1000000);
        const auto attempts = number(argv[4], 10000000);
        const auto service = number(argv[5], 1000000);
        const auto payload_bytes = number(argv[6], 16 * 1024 * 1024);
        require(workers && capacity && attempts, "workers, capacity and attempts must be positive");
        std::atomic<std::uint64_t> checksum{0};
        std::vector<Sample> samples(mode == "overload" ? 0 : attempts);
        Scheduler scheduler(workers, capacity);
        std::size_t accepted = 0, rejected = 0;
        long before = rss_kib(), saturated = -1;
        double elapsed = 0;

        if (mode == "overload") {
            require(attempts >= capacity, "overload attempts must fill capacity");
            std::promise<void> release;
            auto gate = release.get_future().share();
            std::atomic<std::size_t> entered{0};
            // Always release running tasks before unwinding into the pool destructor.
            try {
                for (std::size_t i = 0; i < workers; ++i) {
                    scheduler.submit([&] { ++entered; gate.wait(); });
                    while (entered.load() != i + 1) std::this_thread::yield();
                }
                before = rss_kib(); // Includes started workers, excludes waiting payloads.
                for (std::size_t i = 0; i < attempts; ++i) {
                    try {
                        std::vector<unsigned char> payload(payload_bytes, 1);
                        scheduler.submit([payload = std::move(payload), &checksum] {
                            std::uint64_t sum = 0;
                            for (auto byte : payload) sum += byte;
                            checksum.fetch_add(sum, std::memory_order_relaxed);
                        });
                        ++accepted;
                    } catch (const Scheduler::QueueFull&) { ++rejected; }
                }
                saturated = rss_kib();
                const auto stats = scheduler.stats();
                require(stats.queued == capacity && stats.active == workers, "saturation not established");
                require(accepted == capacity && accepted + rejected == attempts, "overload accounting");
                release.set_value();
            } catch (...) {
                release.set_value();
                throw;
            }
            scheduler.shutdown();
            const auto stats = scheduler.stats();
            require(stats.completed == accepted + workers && stats.failed == 0 &&
                    stats.rejected == rejected, "overload completion accounting");
        } else {
            const auto start = Clock::now();
            for (std::size_t i = 0; i < attempts; ++i) {
                const auto submitted = Clock::now();
                try {
                    std::vector<unsigned char> payload(payload_bytes, 1);
                    scheduler.submit([&, i, submitted, payload = std::move(payload)] {
                        const auto began = Clock::now();
                        if (mode == "blocking") std::this_thread::sleep_for(std::chrono::microseconds(service));
                        else {
                            const auto until = began + std::chrono::microseconds(service);
                            while (Clock::now() < until) std::atomic_signal_fence(std::memory_order_seq_cst);
                        }
                        std::uint64_t sum = 0;
                        for (auto byte : payload) sum += byte;
                        checksum.fetch_add(sum, std::memory_order_relaxed);
                        const auto ended = Clock::now();
                        samples[i] = {Micros(began - submitted).count(), Micros(ended - submitted).count(), true};
                    });
                    ++accepted;
                } catch (const Scheduler::QueueFull&) { ++rejected; }
            }
            scheduler.shutdown();
            elapsed = std::chrono::duration<double>(Clock::now() - start).count();
            const auto stats = scheduler.stats();
            require(stats.accepted == accepted && stats.completed == accepted && stats.failed == 0 &&
                    stats.rejected == rejected && accepted + rejected == attempts, "burst accounting");
        }
        std::vector<double> queue, total;
        for (const auto& sample : samples) if (sample.finished) {
            queue.push_back(sample.queue_us);
            total.push_back(sample.total_us);
        }
        if (mode != "overload") require(queue.size() == accepted, "missing latency sample");
        const auto stats = scheduler.stats();
        require(stats.queued == 0 && stats.active == 0, "pool did not drain");
        require(checksum == static_cast<std::uint64_t>(accepted) * payload_bytes, "payload checksum");
        std::cout << "mode,workers,capacity,attempts,service_us,payload_bytes,accepted,rejected,elapsed_s,completed_per_s,queue_p50_us,queue_p95_us,queue_p99_us,total_p50_us,total_p95_us,total_p99_us,rss_before_kib,rss_saturated_kib,rss_delta_kib\n";
        std::cout << std::fixed << std::setprecision(3) << mode << ',' << workers << ',' << capacity
                  << ',' << attempts << ',' << service << ',' << payload_bytes << ',' << accepted
                  << ',' << rejected << ',' << elapsed << ',' << (elapsed > 0 ? accepted / elapsed : 0)
                  << ',' << percentile(queue, .5) << ',' << percentile(queue, .95) << ',' << percentile(queue, .99)
                  << ',' << percentile(total, .5) << ',' << percentile(total, .95) << ',' << percentile(total, .99)
                  << ',' << before << ',' << saturated << ','
                  << (before >= 0 && saturated >= 0 ? saturated - before : -1) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
