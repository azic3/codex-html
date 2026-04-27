#include "threadpool.h"

#include <stdexcept>

ThreadPool::ThreadPool(std::size_t thread_count)
    : m_stopped(false)
{
    if (thread_count == 0)
    {
        thread_count = 4;
    }

    for (std::size_t i = 0; i < thread_count; ++i)
    {
        m_workers.push_back(std::thread(&ThreadPool::worker_loop, this));
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_stopped = true;
    }
    m_condition.notify_all();

    for (std::size_t i = 0; i < m_workers.size(); ++i)
    {
        if (m_workers[i].joinable())
        {
            m_workers[i].join();
        }
    }
}

void ThreadPool::enqueue(const std::function<void()> &task)
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_stopped)
        {
            throw std::runtime_error("thread pool already stopped");
        }
        m_tasks.push(task);
    }
    m_condition.notify_one();
}

void ThreadPool::worker_loop()
{
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            while (!m_stopped && m_tasks.empty())
            {
                m_condition.wait(lock);
            }

            if (m_stopped && m_tasks.empty())
            {
                return;
            }

            task = m_tasks.front();
            m_tasks.pop();
        }

        task();
    }
}
