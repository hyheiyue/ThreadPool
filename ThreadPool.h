// Copyright 2025 XiaoJian Wu
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

/**
 * @brief A dynamic task-scheduling thread pool with priority queue support, overload protection,
 *        and task execution time monitoring.
 */
class ThreadPool {
public:
    /**
     * @brief Construct a new Thread Pool object
     * 
     * @param num_threads          Number of worker threads to launch.
     * @param max_pending_tasks    Maximum number of tasks allowed in queue before dropping.
     * @param max_task_duration_ms Max allowed task duration before warning (in ms).
     */
    explicit ThreadPool(
        size_t num_threads,
        size_t max_pending_tasks = 100,
        int max_task_duration_ms = 100
    ):
        stop_(false),
        max_pending_tasks_(max_pending_tasks),
        max_task_duration_ms_(max_task_duration_ms) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this]() { this->workerThread(); });
        }

        // Launch the background control thread that auto-tunes the queue size.
        controller_ = std::thread([this]() { this->controlThread(); });
    }

    /**
     * @brief Destroy the Thread Pool object, waits for all tasks to finish.
     */
    ~ThreadPool() {
        waitUntilEmpty();

        {
            std::unique_lock<std::mutex> lock(mutex_);
            stop_ = true;
        }

        cond_var_.notify_all();

        if (controller_.joinable()) {
            controller_.join();
        }

        for (std::thread& worker: workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    /**
     * @brief Enqueue a task into the thread pool.
     * 
     * @tparam F Task type (usually a lambda or function object).
     * @param f The task to enqueue.
     * @param high_priority Whether to put it at the front of the queue.
     */
    template<class F>
    void enqueue(F&& f, bool high_priority = false) {
        {
            std::unique_lock<std::mutex> lock(mutex_);

            // Drop oldest task if queue is full
            if (tasks_.size() >= max_pending_tasks_) {
                tasks_.pop_front();
                std::cerr << "[ThreadPool] Warning: Dropped oldest pending task" << std::endl;
            }

            TaskItem task;
            task.func = std::forward<F>(f);
            task.high_priority = high_priority;

            if (high_priority) {
                tasks_.emplace_front(std::move(task));
            } else {
                tasks_.emplace_back(std::move(task));
            }
        }

        cond_var_.notify_one();
    }

    /**
     * @brief Get the number of tasks currently waiting in the queue.
     * 
     * @return size_t Number of pending tasks.
     */
    size_t pendingTasks() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return tasks_.size();
    }

    /**
     * @brief Block until all tasks are completed and the queue is empty.
     */
    void waitUntilEmpty() {
        std::unique_lock<std::mutex> lock(mutex_);
        task_done_cv_.wait(lock, [this]() { return tasks_.empty() && busy_workers_ == 0; });
    }

private:
    struct TaskItem {
        std::function<void()> func;
        bool high_priority;
    };

    void workerThread() {
        while (true) {
            TaskItem task;

            {
                std::unique_lock<std::mutex> lock(mutex_);
                cond_var_.wait(lock, [this]() { return this->stop_ || !this->tasks_.empty(); });

                if (stop_ && tasks_.empty())
                    return;

                task = std::move(tasks_.front());
                tasks_.pop_front();
                ++busy_workers_;
            }

            auto start_time = std::chrono::steady_clock::now();
            task.func();
            auto end_time = std::chrono::steady_clock::now();

            auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time)
                    .count();
            if (duration > max_task_duration_ms_) {
                std::cerr << "[ThreadPool] Warning: Task took too long: "
                          << duration << " ms" << std::endl;
            }

            {
                std::unique_lock<std::mutex> lock(mutex_);
                --busy_workers_;
                if (tasks_.empty() && busy_workers_ == 0) {
                    task_done_cv_.notify_all();
                }
            }
        }
    }

    void controlThread() {
        using namespace std::chrono_literals;

        while (!stop_) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                size_t pending = tasks_.size();

                if (pending > max_pending_tasks_ * 0.8) {
                    max_pending_tasks_ = std::max<size_t>(10, max_pending_tasks_ * 0.8);
                    std::cerr << "[ThreadPool] Warning: Queue overloaded, shrink to "
                              << max_pending_tasks_ << std::endl;
                } else if (pending < max_pending_tasks_ * 0.3) {
                    max_pending_tasks_ = std::min<size_t>(500, max_pending_tasks_ + 5);
                }
            }
            std::this_thread::sleep_for(500ms);
        }
    }

    std::vector<std::thread> workers_;
    std::deque<TaskItem> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cond_var_;
    std::condition_variable task_done_cv_;
    std::atomic<size_t> busy_workers_ { 0 };
    bool stop_;
    size_t max_pending_tasks_;
    int max_task_duration_ms_;
    std::thread controller_;
};

/**
 * @brief Set the current thread's real-time priority (Linux only).
 * 
 * @param priority The real-time priority level (default: 90).
 */
inline void SetRealtimePriority(int priority = 90) {
    pthread_t this_thread = pthread_self();
    struct sched_param schedParams;
    schedParams.sched_priority = priority;

    int ret = pthread_setschedparam(this_thread, SCHED_FIFO, &schedParams);
    if (ret != 0) {
        std::cerr << "[ThreadPool] Error: Failed to set real-time priority. Code: " << ret << std::endl;
        perror("pthread_setschedparam");
    } else {
        std::cerr << "[ThreadPool] Info: Real-time priority set to " << priority << std::endl;
    }
}
