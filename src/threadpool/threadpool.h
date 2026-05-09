#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool
{
public:
    explicit ThreadPool(std::size_t thread_count);
    ~ThreadPool();

    void enqueue(const std::function<void()> &task);

private:
    void worker_loop();

private:
    std::vector<std::thread> m_workers;
    std::queue<std::function<void()> > m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_stopped;
};

#endif
