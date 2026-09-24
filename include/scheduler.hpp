#pragma once
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class Scheduler {
public:
    struct Ticket { std::uint64_t id; std::future<void> result; };
    explicit Scheduler(std::size_t workers);
    ~Scheduler();
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Ticket submit(std::function<void()> work);
    bool cancel(std::uint64_t id); // Only queued tasks can be cancelled.
private:
    struct Task { std::uint64_t id; std::function<void()> work; std::promise<void> promise; };
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<Task>> queue_;
    std::vector<std::thread> workers_;
    std::uint64_t next_id_ = 1;
    bool stopping_ = false;
    void run();
};
