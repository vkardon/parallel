//
// main.cpp
//
#include "processPool.hpp"
#include <iostream>
#include <csignal>
#include <chrono>
#include <thread>

class TestPool : public TaskPool
{
    // Logging
    virtual void OnInfo(const std::string& msg) const { std::cout << msg << std::endl; }
    virtual void OnError(const std::string& msg) const { std::cout << msg << std::endl; }
};

//
// A "stubborn" task that ignores SIGTERM to test the pool's escalation logic.
// This forces the parent to eventually use SIGKILL.
//
int StubbornTask(int id)
{
    // Explicitly ignore SIGTERM
    std::signal(SIGTERM, SIG_IGN);
    
    std::cout << "  [Child " << id << "] Started (PID: " << getpid() << "). Ignoring SIGTERM..." << std::endl;
    
    // Simulate work that takes longer than the parent's grace period
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::cout << "  [Child " << id << "] Finished work." << std::endl;
    return 0; // Return child process exit code 0 (success)
}

int main()
{
    // Explicitly ignore SIGCHLD. This usually breaks standard waitpid() calls.
    // Our ProcessPool is designed to handle this via waitid + ECHILD checks.
    std::signal(SIGCHLD, SIG_IGN);
 
    //
    // TEST 1: The "Hostile" Environment (SIGCHLD Ignored)
    //
    std::cout << "\n[Main] --- Test 1: SIGCHLD is ignored (Kernel auto-reaping) ---" << std::endl;

    {
        // Run 3 tasks, but only 2 at a time to test throttling
        std::cout << "[Main] Starting TaskPool (3 tasks, 2 concurrent)..." << std::endl;
        
        TestPool pool;
        bool result = pool.Create(StubbornTask, 3, 2);

        if(result)
        {
            std::cout << "[Main] --- Test 1: SUCCESS - Tasks completed despite SIG_IGN environment." << std::endl;
        }
        else
        {
            std::cout << "[Main] --- Test 1: FAILED - Pool reported failure." << std::endl;
        }
    }

    //
    // TEST 2: TerminateAll() Escalation (Force Kill)
    //
    std::cout << "\n[Main] --- Test 2: TerminateAll escalation (Force Kill) ---" << std::endl;

    {
        // This task will ignore SIGTERM and sleep forever
        TestPool pool;

        // We spawn few children.
        bool result = pool.Create([](int id) -> int
        {
            if (id == 0) 
            {
                // Child 0 fails immediately
                std::cout << "  [Child 0] I am failing now to trigger a pool shutdown." << std::endl;
                return 5; // Return exit code other then 0 to similate failure
            }
            else 
            {
                // Child 1 is stubborn and stays alive
                std::signal(SIGTERM, SIG_IGN); 
                std::cout << "  [Child " << id << "] I am stubborn. I will ignore SIGTERM." << std::endl;
                while(true) 
                { 
                    pause(); // Wait for signals
                } 
                return 0;
            }
        }, 3, 3);
        
        if(result)
        {
            std::cout << "[Main] --- Test 2: FAILURE - Pool reported success but Child 0 should have failed." << std::endl;
        }
        else
        {
            std::cout << "[Main] --- Test 2: SUCCESS - Pool correctly caught failure and shut down." << std::endl;
        }
    }

    std::cout << "\n[Main] Demo finished safely." << std::endl;
    return 0;
}