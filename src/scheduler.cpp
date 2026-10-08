#include "scheduler.hpp"
#include <algorithm>
#include <limits>
#include <optional>

thread_local const Scheduler* Scheduler::current_worker_ = nullptr;

Scheduler::Scheduler(std::size_t workers, std::size_t capacity)
    : Scheduler(workers, capacity, [](std::function<void()> entry) {
        return std::thread(std::move(entry));
    }) {}

Scheduler::Scheduler(std::size_t workers, std::size_t capacity, const WorkerFactory& factory)
    : capacity_(capacity) {
    if (!workers || !capacity) throw std::invalid_argument("workers and capacity must be positive");
    workers_.reserve(workers);
    try {
        for (std::size_t i = 0; i < workers; ++i)
            workers_.push_back(factory([this] { run(); }));
    } catch (...) {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        cv_.notify_all();
        for (auto& worker : workers_) worker.join();
        throw;
    }
}

Scheduler::~Scheduler() {
    // Destroying the scheduler from its own task violates the lifetime contract.
    if (current_worker_ == this) std::terminate();
    shutdown();
}

Scheduler::Ticket Scheduler::submit(std::function<void()> work) {
    if (!work) throw std::invalid_argument("empty task");
    // Keep user-owned callable destruction outside the queue lock, including rejection.
    auto task = std::make_shared<Task>();
    task->work = std::move(work);
    auto result = task->promise.get_future();
    {
        std::lock_guard lock(mutex_);
        if (stopping_) { ++rejected_; throw Stopped{}; }
        if (queue_.size() == capacity_) { ++rejected_; throw QueueFull{}; }
        if (next_id_ == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("task IDs exhausted");
        task->id = next_id_;
        queue_.push_back(task); // Allocation failure leaves counters/ID unchanged.
        ++next_id_;
        ++accepted_;
    }
    cv_.notify_one();
    return {task->id, std::move(result)};
}

bool Scheduler::cancel(std::uint64_t id) {
    std::shared_ptr<Task> task;
    {
        std::lock_guard lock(mutex_);
        auto it = std::find_if(queue_.begin(), queue_.end(), [id](const auto& t) { return t->id == id; });
        if (it == queue_.end()) return false;
        task = *it;
        queue_.erase(it);
        task->promise.set_exception(cancelled_error_);
        ++cancelled_;
    }
    return true;
}

void Scheduler::request_stop(StopMode mode) {
    std::optional<std::deque<std::shared_ptr<Task>>> abandoned;
    if (mode == StopMode::cancel_pending) abandoned.emplace();
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        if (mode == StopMode::cancel_pending) {
            abandoned->swap(queue_);
            for (auto& task : *abandoned) task->promise.set_exception(cancelled_error_);
            cancelled_ += abandoned->size();
        }
    }
    cv_.notify_all();
    // Captures may execute user destructors, so release them outside mutex_.
}

void Scheduler::shutdown(StopMode mode) {
    if (current_worker_ == this)
        throw std::logic_error("a worker must use request_stop, not shutdown");
    request_stop(mode);
    std::lock_guard join_lock(join_mutex_);
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
}

Scheduler::Stats Scheduler::stats() const {
    std::lock_guard lock(mutex_);
    return {queue_.size(), active_, capacity_, accepted_, completed_, failed_, cancelled_, rejected_, stopping_};
}

void Scheduler::run() {
    current_worker_ = this;
    for (;;) {
        std::shared_ptr<Task> task;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) break;
            task = queue_.front(); queue_.pop_front();
            ++active_;
        }
        std::exception_ptr error;
        try { task->work(); } catch (...) { error = std::current_exception(); }
        {
            std::lock_guard lock(mutex_);
            --active_;
            if (error) { ++failed_; task->promise.set_exception(error); }
            else { ++completed_; task->promise.set_value(); }
        }
    }
    current_worker_ = nullptr;
}
