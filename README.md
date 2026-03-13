**A collection of header-only C++ libraries implementing common parallel processing design patterns, including process pools, thread pools, and pipelines.**

---
# ProcessPool

A lightweight, robust C++ library for Linux designed to manage high-volume process execution with strict resource throttling. This ProcessPool handles spawning, monitoring, and cleaning up child processes while ensuring your system isn't overwhelmed by "fork bombs" or orphaned zombies.

---

## 🚀 Features

* **Fixed-Size Worker Pool:** Spawn $N$ total tasks while ensuring no more than $M$ are active at any given time.
* **Real-time Monitoring:** Uses `poll()` and Unix pipes to detect worker completion or crashes instantly.
* **Fail-Fast Architecture:** Automatically triggers a global shutdown (`SIGTERM`) of all active workers if a single process fails, preventing inconsistent state.
* **Zombie Prevention:** Proactively reaps finished processes using `waitid` and `O_CLOEXEC` to keep the process table clean.
* **Parent-Death Suicide Pact:** Utilizes `PR_SET_PDEATHSIG` so that if the parent manager dies unexpectedly, all child processes are automatically terminated.

---

## 🛠 How It Works

The `ProcessPool` acts as a governor for your application's subprocesses:

1.  **Throttling:** It iterates through the total child processes count, but blocks if the active child count reaches `maxSimultaneous`.
2.  **Communication:** Each child is born with a pipe. It reports a `ChildStatus` struct (exit code and health bit) just before exiting.
3.  **Cleanup:** The ProcessPool continuously monitors these pipes. If a pipe closes without a "healthy" report, the ProcessPool a crash and halts the entire pool.



---

## 💻 Usage Example

Integrating the ProcessPool into your project is straightforward. Define a worker function and tell the ProcessPool how many times to run it.

```cpp
#include "ProcessPool.hpp"

// Define the task for the child processes
int MyHeavyTask(int id)
{
    std::cout << "Worker " << id << " is processing..." << std::endl;
    sleep(2); // Simulate work
    return 0; // Child process exit code
}

int main()
{
    // Total Tasks: 10, Max Simultaneous: 3
    TaskPool pool;
    pool.Create(MyHeavyTask, 10, 3);
    
    return 0;
}
```
---
# Pipe

A thread-safe C++ implementation of a pipe allowing multiple threads to concurrently write data to one end and multiple threads to concurrently read data from the other. It provides a mechanism for ordered, asynchronous data transfer in high-throughput parallel applications.

---
# ThreadPool

Simple and efficient c++ implementation of thread pool design pattern.
Check example.cpp for detailed example.

Check my another repository, dircpy, to see ThreadPool in action.
