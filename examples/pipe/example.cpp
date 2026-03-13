//
// example.cpp
//
#include <iostream>         // std::cout
#include <atomic>           // std::atomic
#include <mutex>            // std::mutex
#include <thread>           // std::thread
#include <vector>           // std::vector
#include <unistd.h>         // syscall()
#include <sys/syscall.h>    // __NR_gettid
#include "pipe.hpp"

// Helper macro to synchronize writing to std::cout
static std::mutex sLogMutex;
#define TRACE(msg) \
{ \
     std::unique_lock<std::mutex> lock(sLogMutex); \
     std::cout << "[tid=" << syscall(__NR_gettid) << "] " << msg << std::endl; \
}

void TestPipe()
{
    std::cout << ">>> Begin Of Pipe test" << std::endl;

    Pipe<int> pipe(15); // Small pipe capacity for test

    std::atomic<int> writeCount{0};
    std::atomic<int> readCount{0};

    // Create threads to write to the pipe
    std::vector<std::thread> writer(5); // 5 threads
    for(auto& thread : writer)
    {
        thread = std::thread([&]()
        {
            for(int i = 0; i < 20; i++)
            {
                usleep((random() % 100) * 1000); // Add a random 0-100 ms delay
                pipe.Push(++writeCount);
            }
        });
    }

    // Create threads to read from the pipe
    std::vector<std::thread> reader(10); // 10 threads
//    std::vector<std::thread> reader(1);
    for(auto& thread : reader)
    {
        thread = std::thread([&]()
        {
            int data{0};
            while(pipe.Pop(data))
            {
                // Do something here...
                //usleep((random() % 5) * 1000); // Add a random 0-4 ms delay
                usleep((random() % 300) * 1000); // Add a random 0-300 ms delay
                TRACE("data=" << data);
                ++readCount;
            }
        });
    }

    // Wait for writer threads to complete
    for(auto& thread : writer)
        thread.join();

    pipe.SetHasMore(false); // No more data to write
    TRACE("Writer is done");

    // Wait for reader threads to complete
    for(auto& thread : reader)
        thread.join();

    TRACE("Reader is done");

    TRACE("Total writeCount=" << writeCount << ", readCount=" << readCount);

    std::cout << ">>> End Of Pipe test" << std::endl;
}

int main()
{
    TestPipe();
    return 0;
}

