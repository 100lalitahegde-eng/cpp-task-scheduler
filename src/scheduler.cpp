#include "scheduler.hpp"
#include <algorithm>
#include <stdexcept>

Scheduler::Scheduler(std::size_t workers) {
    if (!workers) throw std::invalid_argument("workers must be positive");
    for (std::size_t i = 0; i < workers; ++i) workers_.emplace_back([this] { run(); });
}

Scheduler::~Scheduler() {
    { std::lock_guard lock(mutex_); stopping_ = true; }
    cv_.notify_all();
    for (auto& worker : workers_) worker.join();
}

Scheduler::Ticket Scheduler::submit(std::function<void()> work) {
    if (!work) throw std::invalid_argument("empty task");
    auto task = std::make_shared<Task>();
    task->work = std::move(work);
    auto result = task->promise.get_future();
    { std::lock_guard lock(mutex_);
      if (stopping_) throw std::runtime_error("scheduler stopped");
      task->id = next_id_++;
      queue_.push_back(task);
    }
    cv_.notify_one();
    return {task->id, std::move(result)};
}

bool Scheduler::cancel(std::uint64_t id) {
    std::shared_ptr<Task> task;
    { std::lock_guard lock(mutex_);
      auto it = std::find_if(queue_.begin(), queue_.end(), [id](const auto& t) { return t->id == id; });
      if (it == queue_.end()) return false;
      task = *it;
      queue_.erase(it);
    }
    task->promise.set_exception(std::make_exception_ptr(std::runtime_error("cancelled")));
    return true;
}

void Scheduler::run() {
    for (;;) {
        std::shared_ptr<Task> task;
        { std::unique_lock lock(mutex_);
          cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
          if (stopping_ && queue_.empty()) return;
          task = queue_.front(); queue_.pop_front();
        }
        try { task->work(); task->promise.set_value(); }
        catch (...) { task->promise.set_exception(std::current_exception()); }
    }
}
