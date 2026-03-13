//
// main.cpp
//
#include <iostream>
#include <string.h>
#include <unistd.h>
#include "processPool.hpp"

void RunProcessPool()
{
    std::cout << ">>> " << __func__ << ": Beginning of ProcessPool Run" << std::endl;

    // Note: Create() is blocked for a parent process, it doesn't 
    // return until all child processes stopped.
    ProcessPool procPool;
    if(!procPool.Create(4)) // 4 processes
    {
        std::cout << ">>> " << __func__ << ": ProcessPool::Create() failed" << std::endl;
        return;
    }

    // Are we a child process?
    if(procPool.IsChild())
    {
        // Seed the random number generator uniquely for each process
        srandom(time(NULL) ^ (getpid() << 16));
        
        // Do something here...
        for(int i = 0; i < 20; i++)
        {
            usleep((random() % 5) * 1000); // Add a random 0-4 ms delay
            std::cout << "[child" << procPool.GetChildIndex() << "][pid=" << getpid() << "]"
                    << " Do something... " << i << std::endl;
        }

        // Exit child process
        procPool.Exit(0);
    }

    // If we are here then we must be a parent process and all child processes 
    // completed (either exited or crushed).
    std::cout << ">>> " << __func__ << ": End of ProcessPool Run" << std::endl;
}

void RunTaskPool()
{
    std::cout << ">>> " << __func__ << ": Beginning of ProcessPool Run" << std::endl;

    // Define the child task
    // Note: The lambda signature matches our TaskPool requirement: bool(int)
    auto childTask = [&](int childIndex) -> int
    {
        // Seed the random number generator uniquely for each process
        srandom(time(NULL) ^ (getpid() << 16));

        // Do something here...
        for(int i = 0; i < 20; i++)
        {
            usleep((random() % 5) * 1000); // Add a random 0-4 ms delay
            std::cout << "[child " << childIndex << "][pid=" << getpid() << "]"
                    << " Do something... " << i << std::endl;
        }

        // Exit child process
        return 0;
    };

    // Note: Create() is blocked for a parent process, it doesn't 
    // return until all child processes stopped.
    TaskPool taskPool;
    if(!taskPool.Create(childTask, 4)) // 4 processes
    {
        std::cout << ">>> " << __func__ << ": TaskPool::Create() failed" << std::endl;
        return;
    }

    std::cout << ">>> " << __func__ << ": End of TaskPool Run" << std::endl;
}

int main()
{
    RunProcessPool();
    RunTaskPool();
    return 0;
}

