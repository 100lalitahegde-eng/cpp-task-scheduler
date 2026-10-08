#pragma once
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

class Scheduler {
public:
    struct QueueFull : std::runtime_error { QueueFull() : runtime_error("queue full") {} };
    struct Stopped : std::runtime_error { Stopped() : runtime_error("scheduler stopped") {} };
    struct Cancelled : std::runtime_error { Cancelled() : runtime_error("cancelled") {} };
    struct Ticket { std::uint64_t id; std::future<void> result; };
    enum class StopMode { drain, cancel_pending };
    struct Stats {
        std::size_t queued, active, capacity;
        std::uint64_t accepted, completed, failed, cancelled, rejected;
        bool stopping;
    };
    explicit Scheduler(std::size_t workers, std::size_t queue_capacity = 1024);
    ~Scheduler();
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Ticket submit(std::function<void()> work);
    bool cancel(std::uint64_t id);
    // Safe from tasks; closes admission without joining. Cancellation can upgrade drain.
    void request_stop(StopMode mode = StopMode::drain);
    // External callers only. Concurrent calls serialize joining, not stop requests.
    void shutdown(StopMode mode = StopMode::drain);
    Stats stats() const;
private:
    friend struct SchedulerTestAccess;
    using WorkerFactory = std::function<std::thread(std::function<void()>)>;
    Scheduler(std::size_t workers, std::size_t capacity, const WorkerFactory& factory);
    struct Task { std::uint64_t id; std::function<void()> work; std::promise<void> promise; };
    mutable std::mutex mutex_;
    std::mutex join_mutex_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<Task>> queue_;
    std::vector<std::thread> workers_;
    const std::size_t capacity_;
    const std::exception_ptr cancelled_error_ = std::make_exception_ptr(Cancelled{});
    std::uint64_t next_id_ = 1;
    std::uint64_t accepted_ = 0, completed_ = 0, failed_ = 0, cancelled_ = 0, rejected_ = 0;
    std::size_t active_ = 0;
    bool stopping_ = false;
    static thread_local const Scheduler* current_worker_;
    void run();
};
