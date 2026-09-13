// 负责人：成员1：服务端架构/组长
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace cloud::server {

class TaskRejected final : public std::runtime_error {
public:
    TaskRejected() : std::runtime_error("server task queue is full") {}
};

// Fixed-size executor with a bounded queue. It deliberately rejects excess
// work instead of creating unbounded detached threads or retaining request
// bodies in memory indefinitely.
class TaskExecutor {
public:
    TaskExecutor(std::size_t workerCount, std::size_t queueCapacity);
    ~TaskExecutor();
    TaskExecutor(const TaskExecutor&) = delete;
    TaskExecutor& operator=(const TaskExecutor&) = delete;

    bool tryPost(std::function<void()> task);

    template <typename Function>
    auto submit(Function&& function)
        -> std::future<std::invoke_result_t<std::decay_t<Function>>> {
        using Result = std::invoke_result_t<std::decay_t<Function>>;
        auto task = std::make_shared<std::packaged_task<Result()>>(
            std::forward<Function>(function));
        auto result = task->get_future();
        if (!tryPost([task] { (*task)(); })) throw TaskRejected();
        return result;
    }

private:
    void workerLoop();

    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    std::size_t queueCapacity_{};
    bool stopping_{};
};

} // namespace cloud::server
