# ThreadPool

A dynamic task-scheduling thread pool with priority queue support, overload protection, and task execution time monitoring.

## Overview

- **Dynamic number of worker threads**: Created on construction.

- **Priority queue support**: Tasks can be enqueued with high priority and inserted at the front of the queue.

- **Overload protection**: If the queue exceeds a maximum size, oldest tasks are dropped with a warning.

- **Execution time monitoring**: Warns if a task runs longer than a specified threshold.

- **Background controller thread**: Auto-tunes the maximum queue size based on workload.

- **Wait for all tasks completion**: Supports blocking until all queued tasks finish.

---

## Usage

### Construct

ThreadPool(size_t num_threads, size_t max_pending_tasks = 100, int max_task_duration_ms = 100)

- `num_threads`: Number of worker threads.

- `max_pending_tasks`: Max tasks allowed in the queue before dropping old ones.

- `max_task_duration_ms`: Warning threshold for task execution time in milliseconds.

### Enqueue Tasks

```cpp
pool.enqueue([]() {
    // task code here
}, high_priority = false);
```


Pass a callable as the first argument and optionally a boolean to mark as high priority.

### Query Queue Size

```cpp
size_t pending = pool.pendingTasks();
```


### Wait for Completion

```cpp
pool.waitUntilEmpty();
```


Blocks the caller until all tasks have finished executing.

---

## Additional Function

### SetRealtimePriority(int priority = 90)

Set the current thread's real-time scheduling priority on Linux.

---

## Example

```cpp
#include "ThreadPool.hpp"

int main() {
    ThreadPool pool(4, 200, 150);

    pool.enqueue([]() {
        std::cout << "Task 1 running\n";
    });

    pool.enqueue([]() {
        std::cout << "High priority task running\n";
    }, true);

    pool.waitUntilEmpty();

    return 0;
}
```


---

## Internal Details

- Uses `std::deque` for task storage with `TaskItem` holding the function and priority flag.

- Worker threads wait on a condition variable, pop tasks, execute them, and track busy count.

- If a task runs longer than `max_task_duration_ms_`, a warning is printed.

- A controller thread monitors queue length and auto-adjusts `max_pending_tasks_` to control overload.

- Dropping oldest tasks happens when queue exceeds `max_pending_tasks_`.

- Uses mutex and condition variables to synchronize access to the queue and worker state.

---

## Logging

Warnings and info messages are output to `std::cerr`.

---

# Source Code Snippet (for reference)

```cpp

// Partial code example for enqueue and worker thread loop

template<class F>
void enqueue(F&& f, bool high_priority = false) {
    {
        std::unique_lock<std::mutex> lock(mutex_);

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
            std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
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
```


---

# Notes

- Designed for Linux systems, especially the `SetRealtimePriority` function.

- Suitable for workloads where task priority and overload handling are important.

- Provides basic monitoring to detect slow tasks.



