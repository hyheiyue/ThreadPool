# ThreadPool - A Dynamic Thread Pool with Advanced Features

## Overview

The `ThreadPool` class is a modern and feature-rich implementation of a dynamic thread pool in C++20. It is designed to efficiently handle task execution in a multi-threaded environment, providing support for interruptible tasks, priority-based scheduling, task timeouts, and dynamic queue management. This class is built using modern C++20 features such as `std::jthread`, `std::stop_token`, and `std::promise`, making it highly efficient, flexible, and easy to use in concurrent programming scenarios.

### Key Features:

- **Interruptible Tasks**: Supports tasks that can be interrupted by a stop signal (`std::stop_token`), allowing tasks to be gracefully cancelled if needed.

- **Priority-based Task Scheduling**: Tasks can be enqueued with a priority (high or low), ensuring that important tasks are executed first.

- **Task Timeout Interrupts**: Tasks can have an associated timeout. If the task exceeds the specified timeout, it will be interrupted.

- **Dynamic Queue Size Adjustment**: The thread pool automatically adjusts the queue size based on the current task load, shrinking or expanding it as necessary.

- **Task Execution Monitoring**: Provides a mechanism to wait until all tasks are completed, and monitors the execution state of tasks.

- **Automatic Thread Management**: Uses `std::jthread` to manage worker threads automatically, ensuring that threads are joined when the pool is destroyed or when all tasks are completed.

## Table of Contents

1. [Class Usage](#class-usage)

  - [Constructor](#constructor)

  - [Destructor](#destructor)

  - [Enqueue Task](#enqueue-task)

  - [Wait for All Tasks to Complete](#wait-for-all-tasks-to-complete)

  - [Get Pending Tasks Count](#get-pending-tasks-count)

2. [How It Works](#how-it-works)

  - [Task Enqueueing](#task-enqueueing)

  - [Worker Threads](#worker-threads)

  - [Dynamic Queue Management](#dynamic-queue-management)

  - [Graceful Shutdown](#graceful-shutdown)

3. [Example Usage](#example-usage)

4. [License](#license)

---

## Class Usage

### Constructor

```cpp
explicit ThreadPool(size_t num_threads, size_t max_pending_tasks = 100);
```


- **num_threads**: The number of worker threads to be created in the pool. This determines the level of parallelism available in the pool.

- **max_pending_tasks**: The maximum number of tasks that can be queued in the pool at any given time. If the queue exceeds this limit, older tasks will be dropped to ensure the system remains responsive.

### Destructor

```cpp
~ThreadPool();
```


- The destructor waits for all tasks to complete and ensures that all worker threads are properly joined. If any tasks are still running when the pool is destroyed, it will block until they finish. This ensures no task is left unfinished.

### Enqueue Task

```cpp
template<class F>
std::future<void> enqueue(F&& f, int timeout_ms = -1, bool high_priority = false);
```


- **f**: The task function to be executed. This can be a regular function (`void your_task()`) or an interruptible function (`void your_task(std::stop_token)`).

- **timeout_ms**: The maximum time (in milliseconds) that the task is allowed to run. If the task takes longer than this time, it will be interrupted.

- **high_priority**: Specifies whether the task should be executed with high priority. High priority tasks are processed before low priority ones.

The `enqueue` method returns a `std::future<void>`, which can be used to monitor the completion of the task.

### Wait for All Tasks to Complete

```cpp
void waitUntilEmpty();
```


- Blocks the calling thread until all tasks in the queue have been completed and there are no busy worker threads.

### Get Pending Tasks Count

```cpp
size_t pendingTasks() const;
```


- Returns the number of tasks that are currently in the task queue, including both pending and running tasks.

---

## How It Works

### Task Enqueueing

Tasks are added to a priority queue, where high-priority tasks are processed before low-priority ones. If the queue exceeds its capacity, older tasks will be dropped to avoid overloading the system. This mechanism ensures that the system remains responsive and doesn't suffer from unprocessed tasks when the load is high.

### Worker Threads

Worker threads continuously monitor the task queue and pick tasks to execute. Each worker thread checks for tasks in the queue, processes them, and signals the completion of the task. If a task has a timeout, a separate timer is started to interrupt the task if it exceeds the specified time.

Worker threads are created using `std::jthread`, which provides automatic handling of thread joining and cancellation, making the implementation cleaner and more efficient than using `std::thread` directly.

### Dynamic Queue Management

A controller thread runs in the background to monitor the task queue's size. If the queue is heavily loaded (more than 80% full), the pool reduces the maximum queue size to alleviate congestion. Conversely, if the queue is lightly loaded (less than 30% full), the pool increases the queue size to accommodate more tasks. This dynamic adjustment helps balance performance and memory usage.

### Graceful Shutdown

When the thread pool is destroyed, it waits for all tasks to finish, ensuring that no tasks are left unfinished. If any worker threads are still busy processing tasks, the destructor will block until all tasks are completed. After that, all threads are stopped, and resources are cleaned up.

---

## Example Usage

### Regular Task

This example demonstrates how to enqueue a simple regular task.

```cpp
ThreadPool pool(4);

auto task = pool.enqueue([]() {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "Task complete!" << std::endl;
});

task.get(); // Wait for task completion
```


### Interruptible Task

This example shows how to enqueue an interruptible task, which can be cancelled if needed.

```cpp
ThreadPool pool(4);

auto task = pool.enqueue([](std::stop_token st) {
    while (!st.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "Working..." << std::endl;
    }
}, 500, true); // Timeout after 500 ms

task.get(); // Wait for task completion
```


### Task with Priority

Here, we demonstrate the usage of priority-based task scheduling. High-priority tasks are processed before low-priority ones.

```cpp
ThreadPool pool(4);

auto low_priority_task = pool.enqueue([]() { std::cout << "Low priority task\n"; }, -1, false);
auto high_priority_task = pool.enqueue([]() { std::cout << "High priority task\n"; }, -1, true);

high_priority_task.get(); // This will be executed first
low_priority_task.get();
```


---

## License

This project is licensed under the Apache License, Version 2.0. You may obtain a copy of the License at:

```
http://www.apache.org/licenses/LICENSE-2.0
```


Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.

