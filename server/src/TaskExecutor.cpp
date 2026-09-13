// 负责人：成员1：服务端架构/组长
#include "cloud/server/TaskExecutor.h"

#include <algorithm>

namespace cloud::server {

TaskExecutor::TaskExecutor(std::size_t workerCount, std::size_t queueCapacity)
    : queueCapacity_(std::max<std::size_t>(1, queueCapacity)) {
    workerCount = std::max<std::size_t>(1, workerCount);
    workers_.reserve(workerCount);
    for (std::size_t index = 0; index < workerCount; ++index)
        workers_.emplace_back(&TaskExecutor::workerLoop, this);
}

TaskExecutor::~TaskExecutor() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    ready_.notify_all();
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
}

bool TaskExecutor::tryPost(std::function<void()> task) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || tasks_.size() >= queueCapacity_) return false;
        tasks_.push_back(std::move(task));
    }
    ready_.notify_one();
    return true;
}

void TaskExecutor::workerLoop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            if (stopping_ && tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        try {
            task();
        } catch (...) {
            // Packaged tasks retain their exception in the returned future.
            // Fire-and-forget connection tasks contain their own error handling.
        }
    }
}

} // namespace cloud::server
