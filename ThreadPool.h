// Copyright 2025 Xiaojian Wu
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
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <vector>

/**
 * @brief Dynamic thread pool with task timeout interrupts, priority scheduling,
 *        overload protection, and execution monitoring using C++20 features.
 * 
 * Features:
 *   - Supports both interruptible (void(std::stop_token)) and regular (void()) tasks
 *   - Priority-based task scheduling (high/low priority)
 *   - Per-task timeout interrupts
 *   - Dynamic queue size adjustment
 *   - Task execution monitoring
 *   - Automatic thread management with std::jthread
 * 
 * Usage:
 *   - For interruptible tasks: void your_task(std::stop_token tok)
 *   - Check tok.stop_requested() for interruption points
 */
class ThreadPool {
public:
    /// Construct thread pool with configurable parameters
    explicit ThreadPool(
        size_t num_threads, ///< Number of worker threads
        size_t max_pending_tasks = 100 ///< Initial max queue capacity
    ):
        stop_(false),
        max_pending_tasks_(max_pending_tasks) {
        // Create worker threads
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this](std::stop_token st) { workerThread(st); });
        }

        // Create controller thread for dynamic adjustments
        controller_ = std::jthread([this](std::stop_token st) { controlThread(st); });
    }

    /// Destructor waits for task completion and stops threads
    ~ThreadPool() {
        waitUntilEmpty(); // Wait for all tasks to finish
        stop_ = true; // Signal termination
        cond_var_.notify_all(); // Wake all workers
        task_done_cv_.notify_all(); // Wake any waiters
        // std::jthread automatically joins on destruction
    }

    /// Enqueue a task with optional priority
    template<class F>
    std::future<void> enqueue(F&& f, int timeout_ms = -1, bool high_priority = false) {
        // Create promise-future pair for task result
        auto prom = std::make_shared<std::promise<void>>();
        std::future<void> fut = prom->get_future();

        // Create dedicated stop source for this task
        std::stop_source src;
        std::stop_token task_tok = src.get_token();

        // Wrap user function to handle different signatures
        auto wrapped = [f = std::forward<F>(f), prom](std::stop_token st) mutable {
            try {
                // Detect function signature and call appropriately
                if constexpr (std::is_invocable_v<F, std::stop_token>) {
                    f(st); // Interruptible version
                } else {
                    f(); // Regular version
                }
                prom->set_value(); // Set result on success
            } catch (...) {
                prom->set_exception(std::current_exception()); // Propagate exceptions
            }
        };

        {
            std::unique_lock lock(mutex_);
            // Apply overload protection - drop oldest task when queue full
            if (tasks_.size() >= max_pending_tasks_) {
                tasks_.pop();
                std::cerr << "[ThreadPool] Warning: Dropped oldest pending task\n";
            }
            // Add task to priority queue
            tasks_.push(TaskItem { std::move(wrapped), high_priority, std::move(src), timeout_ms });
        }

        cond_var_.notify_one(); // Wake one worker
        return fut; // Return future for task result
    }

    /// Get current number of pending tasks
    size_t pendingTasks() const {
        std::unique_lock lock(mutex_);
        return tasks_.size();
    }

    /// Block until all tasks complete and queue is empty
    void waitUntilEmpty() {
        std::unique_lock lock(mutex_);
        task_done_cv_.wait(lock, [this] { return tasks_.empty() && busy_workers_ == 0; });
    }

private:
    /// Task representation in the queue
    struct TaskItem {
        std::function<void(std::stop_token)> func; ///< Wrapped task function
        bool high_priority; ///< Priority flag
        std::stop_source stop_src; ///< Task-specific stop source
        int timeout_ms = 0;

        /// Priority comparison (high priority first)
        bool operator<(TaskItem const& o) const {
            // Priority_queue returns largest first, so invert comparison
            return (!high_priority && o.high_priority);
        }
    };

    /// Worker thread processing loop
    void workerThread(std::stop_token pool_stop) {
        while (!pool_stop.stop_requested()) {
            TaskItem item;
            {
                std::unique_lock lock(mutex_);
                // Wait for task or termination signal
                cond_var_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                // Exit if termination requested and no tasks remain
                if (stop_ && tasks_.empty())
                    return;

                // Get highest priority task
                item = std::move(tasks_.top());
                tasks_.pop();
                ++busy_workers_; // Update busy counter
            }
            std::optional<std::jthread> timer;
            if (item.timeout_ms > 0) {
                timer.emplace([&, ms = item.timeout_ms](std::stop_token st) {
                    if (!st.stop_requested())
                        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                    if (!st.stop_requested())
                        item.stop_src.request_stop();
                });
            }

            // Execute task with its dedicated stop_token
            item.func(item.stop_src.get_token());

            // Stop timeout timer
            if (timer)
                timer->request_stop();

            {
                std::unique_lock lock(mutex_);
                --busy_workers_; // Update busy counter
                // Notify if all work completed
                if (tasks_.empty() && busy_workers_ == 0)
                    task_done_cv_.notify_all();
            }
        }
    }

    /// Controller thread for dynamic adjustments
    void controlThread(std::stop_token pool_stop) {
        using namespace std::chrono_literals;
        while (!pool_stop.stop_requested()) {
            {
                std::unique_lock lock(mutex_);
                size_t pending = tasks_.size();

                // Dynamic queue adjustment logic:
                if (pending > max_pending_tasks_ * 0.8) {
                    // Reduce queue size under heavy load
                    max_pending_tasks_ = std::max<size_t>(10, max_pending_tasks_ * 0.8);
                    std::cerr << "[ThreadPool] Warning: Queue overloaded, shrink to "
                              << max_pending_tasks_ << "\n";
                } else if (pending < max_pending_tasks_ * 0.3) {
                    // Increase queue size during light load
                    max_pending_tasks_ = std::min<size_t>(500, max_pending_tasks_ + 5);
                }
            }
            std::this_thread::sleep_for(500ms); // Adjustment interval
        }
    }

    // Member variables
    std::vector<std::jthread> workers_; ///< Worker threads
    std::priority_queue<TaskItem> tasks_; ///< Priority task queue
    mutable std::mutex mutex_; ///< Synchronization lock
    std::condition_variable cond_var_; ///< Task notification CV
    std::condition_variable task_done_cv_; ///< Completion notification CV
    std::atomic<size_t> busy_workers_ { 0 }; ///< Count of active workers
    std::atomic<bool> stop_ { false }; ///< Global stop flag
    size_t max_pending_tasks_; ///< Current queue capacity
    std::jthread controller_; ///< Control thread
};
