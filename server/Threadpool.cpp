#include "ThreadPool.hpp"

ThreadPool::ThreadPool(size_t numThreads) noexcept : shutdown(false) {
    for (size_t i = 0; i < numThreads; i++) {
        threads.emplace_back(&ThreadPool::worker, this);
    }
}

ThreadPool::~ThreadPool() noexcept {
    shutdown = true;
    not_empty.notify_all();
    for (auto &t: threads) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void ThreadPool::addTask(const std::function<void()>& task) {
    {
        std::lock_guard<std::mutex> lock(pool_lock);
        tasks.push(task);
    }
    not_empty.notify_one();
}

void ThreadPool::worker() {
    std::function<void()> task;
    while (true) {
        {
            std::unique_lock<std::mutex> lock(pool_lock);
            not_empty.wait(lock, [this] { return shutdown || !tasks.empty(); });
            if (shutdown && tasks.empty()) {
                return;
            }
            task = std::move(tasks.front());
            tasks.pop();
        }
        task();
    }
}
