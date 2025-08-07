//
// Created by shawn on 23-8-7/8.
//

#ifndef THREADPOOL_HPP
#define THREADPOOL_HPP

#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>

class ThreadPool {
public:
    ThreadPool(size_t numThreads) noexcept;
    ~ThreadPool() noexcept;
    void addTask(const std::function<void()>& task);
private:
    void worker();
    std::mutex pool_lock;
    std::condition_variable not_empty;
    std::vector<std::thread> threads;
    std::queue<std::function<void()>> tasks;
    std::atomic<bool> shutdown;
};

#endif // THREADPOOL_HPP 