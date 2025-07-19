# ThreadPool - A Dynamic Thread Pool with Advanced Features

`ThreadPool` is a highly flexible and dynamic thread pool implementation for C++20. It supports advanced features such as task prioritization, timeout-based interruptions, overload protection, and dynamic queue resizing. The class utilizes `std::jthread`, `std::stop_token`, and other C++20 features to provide a modern, efficient, and safe thread pool.

## Features

- **Interruptible Tasks**: Supports both regular (void()) and interruptible (void(std::stop_token)) tasks.

- **Priority Scheduling**: Tasks can be enqueued with high or low priority.

- **Task Timeout**: Each task can have a timeout duration, with automatic interruption when it exceeds the limit.

- **Dynamic Queue Size**: The thread pool adjusts the task queue size based on the current load, shrinking or expanding the queue to prevent overload.

- **Task Monitoring**: Monitors task completion, with support for blocking until all tasks are completed.

- **Automatic Thread Management**: Automatically manages worker threads with `std::jthread`.

- **Overload Protection**: If the queue exceeds its maximum capacity, the oldest tasks are dropped to prevent overload.

## Requirements

- C++20 or later

- A C++ compiler supporting `std::jthread` and `std::stop_token` (e.g., GCC 10+, Clang 11+, MSVC 2019+)

## Usage

### Constructing the ThreadPool

You can construct a `ThreadPool` with configurable parameters such as the number of worker threads, the maximum number of pending tasks, and the maximum task timeout duration.

```cpp
ThreadPool pool(4, 100, 100);  // 4 threads, max 100 tasks, 100 ms timeout
```


### Enqueuing Tasks

You can enqueue tasks with or without priority, using the `enqueue` method. The function signature can either accept a stop token for interruptible tasks or no parameters for regular tasks.

#### Regular Task:

```cpp
pool.enqueue([]() {
    // Regular task code
});
```


#### Interruptible Task:

```cpp
pool.enqueue([](std::stop_token tok) {
    while (!tok.stop_requested()) {
        // Interruptible task code
    }
}, true);  // Set high priority
```


The `std::stop_token` is used to check if the task has been requested to stop (due to timeout or external interruption).

### Getting Pending Tasks

You can check how many tasks are pending in the thread pool with the `pendingTasks()` method:

```cpp
size_t num_pending = pool.pendingTasks();
```


### Blocking Until All Tasks Complete

To block until all tasks have completed and the task queue is empty, use the `waitUntilEmpty()` method:

```cpp
pool.waitUntilEmpty();
```


### Destructor

The destructor automatically waits for all tasks to finish and stops all worker threads.

```cpp
~ThreadPool();
```


## Example

```cpp
#include "ThreadPool.h"

int main() {
    // Create a thread pool with 4 worker threads and a maximum of 100 pending tasks
    ThreadPool pool(4, 100, 100);

    // Enqueue a regular task
    pool.enqueue([]() {
        std::cout << "Task 1 completed\n";
    });

    // Enqueue an interruptible task
    pool.enqueue([](std::stop_token tok) {
        while (!tok.stop_requested()) {
            std::cout << "Task 2 running\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    // Block until all tasks are completed
    pool.waitUntilEmpty();
    
    return 0;
}
```


## License

This project is licensed under the Apache License, Version 2.0. See [LICENSE](http://www.apache.org/licenses/LICENSE-2.0) for details.

---

