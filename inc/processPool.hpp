//
// processPool.hpp
//
#ifndef _PROCESS_POOL_HPP_
#define _PROCESS_POOL_HPP_

#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <poll.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>     // assert
#include <iostream>     // std::cout
#include <vector>

//
// Utility class to fork children processes and wait for them to exit
//
class ProcessPool
{
public:
    ProcessPool() = default;
    ~ProcessPool() { TerminateAll(); }

    // Omit implementation of the copy constructor and assignment operator
    ProcessPool(const ProcessPool&) = delete;
    ProcessPool& operator=(const ProcessPool&) = delete;

    // Fork N children but keeps no more than M running at once
    bool Create(int procCount, int maxConcurrentProcs=0);

    // Exit completed child
    void Exit(int exitCode);

    // Are we a parent of a child?
    bool IsParent() const { return (mChildIndex < 0); }
    bool IsChild() const { return !IsParent(); }
    int GetChildIndex() { return mChildIndex; }

protected:
    // Notification sent to derived class to collect statistics, etc
    enum class NOTIFY_TYPE : char
    {
        PRE_FORK=1,         // Send right before forking children
        CHILD_FORK,         // Send right after forking a child
        POST_FORK,          // Send right after forking all children
        CHILDREN_DONE       // Send right after all children done (but might be alive and idle)
    };
    
    virtual void OnNotify(NOTIFY_TYPE /*notifyType*/) {}

    // Logging
    virtual void OnInfo(const std::string& msg) const { /*std::cout << msg << std::endl;*/ }
    virtual void OnError(const std::string& msg) const { std::cout << msg << std::endl; }

private:
    // Structure passed through the pipe to communicate exit status
    struct ChildStatus
    {
        int exitCode;
        bool isHealthy; // false if the process crashed/died without reporting
    };
    
    struct ChildInfo
    {
        pid_t pid;
        int readFd;
        int id;
    };

    // Forks a single worker and sets up the status pipe
    bool ForkChild(int id);

    // Sends SIGTERM to all active children and reaps them
    void TerminateAll();

    // Internal helper to close handles and attempt to reap zombies
    void CleanupChild(ChildInfo& child);

    // Proactively checks for finished children using poll().
    // Returns false if a child failed or crashed.
    bool CheckForFinishedChildren(int timeoutMs);

    // Class data
    std::vector<ChildInfo> mActiveChildren;

    // Zero-based child process index in the order of forking; -1 for the parent
    int mChildIndex{-1};

    // Pipe for a child process to communicate back to a parent
    int mWriteFd{-1};   
};

//
// Helper macros to log info/error messages
//
#include <sstream>  // std:::stringstream

#ifndef PROCESS_POOL_INFO
#define PROCESS_POOL_INFO(msg) \
    do { \
         std::stringstream buf; \
         buf << "[INFO][" << __FILE__ << ":" << __LINE__ << "] " << __func__ << ": " << msg; \
         OnInfo(buf.str()); \
    } while(0);
#endif // PROCESS_POOL_INFO

#ifndef PROCESS_POOL_ERROR
#define PROCESS_POOL_ERROR(msg) \
    do { \
         std::stringstream buf; \
         buf << "[ERROR][" << __FILE__ << ":" << __LINE__ << "] " << __func__ << ": " << msg; \
         OnError(buf.str()); \
    } while(0);
#endif // PROCESS_POOL_ERROR

// Sends SIGTERM to all active children and reaps them
inline void ProcessPool::TerminateAll()
{
    if(mActiveChildren.empty()) 
        return;

    // Signal everyone to stop immediately
    for(auto& child : mActiveChildren)
    {
        if(child.pid > 0)
            kill(child.pid, SIGTERM);
    }

    // Adaptive Grace Period.
    // Note: Almost no process can shut down in microseconds.
    // We need a single small pause to let the SIGTERM "sink in".
    // This allows the children to run their destructors/cleanup logic.
    // Wait up to 100ms, but no longer than needed.
    for(int i = 0; i < 10; ++i) // 10 attempts * 10ms = 100ms max
    {
        bool anyAlive = false;
        for(auto& child : mActiveChildren)
        {
            if(child.pid <= 0) 
                continue;

            // Check if process is still there
            siginfo_t info{};
            int res = waitid(P_PID, (id_t)child.pid, &info, WEXITED | WNOHANG);

            // If res == 0 and si_pid == 0, the process is still running.
            // Otherwise, either the child exited (info.si_pid == child.pid)
            // or the system call failed (res == -1, likely ECHILD).
            // In both cases, the child process is already gone/reaped, we move on.
            if(res == 0 && info.si_pid == 0) 
            {
                // Found one that is still running! No need to check others this round.
                anyAlive = true;
                break;
            }
        }
        
        if(!anyAlive) 
            break; // Everyone is gone, exit the 100ms loop early.

        usleep(10000); // Wait 10ms
    }

    // Final Collection & Force Kill
    for(auto& child : mActiveChildren)
    {
        if(child.pid <= 0) 
            continue;

        siginfo_t info{};
        int res = waitid(P_PID, (id_t)child.pid, &info, WEXITED | WNOHANG);

        // If the process is STILL running after the grace period, SIGKILL it.
        if(res == 0 && info.si_pid == 0)
        {
            PROCESS_POOL_ERROR("Child " << child.id << " (" << child.pid << ") ignored SIGTERM. "
                               "Sending SIGKILL.");
            kill(child.pid, SIGKILL);
        }

        // Close pipes and attempt to reap the zombie.
        // CleanupChild will handle the actual reap or the ECHILD case.
        CleanupChild(child);
    }

    mActiveChildren.clear();
}

inline bool ProcessPool::ForkChild(int id)
{
    // Use O_CLOEXEC so the pipes don't leak if the child calls exec()
    int pipeFds[2]{};
    if(pipe2(pipeFds, O_CLOEXEC) == -1)
    {
        std::string errmsg = strerror(errno);
        PROCESS_POOL_ERROR("Parent " << getpid() << " failed for pipe2() because " << errmsg);
        return false;
    }

    // Flush all parent's open output streams
    fflush(nullptr);

    // Fork a child process
    pid_t childPID = fork();
    if(childPID < 0)
    {
        close(pipeFds[0]);
        close(pipeFds[1]);
        std::string errmsg = strerror(errno);
        PROCESS_POOL_ERROR("Parent " << getpid() << " couldn't fork child " << id << " because " << errmsg);
        return false;
    }

    if(childPID == 0)
    {
        // Child process
        close(pipeFds[0]);
        mWriteFd = pipeFds[1];

        // Parent-death suicide pact
        prctl(PR_SET_PDEATHSIG, SIGTERM);

        // Note: If the parent dies between the fork() and the prctl() call, 
        // the child is adopted by init (PID 1). In this case, exiting immediately
        // is the safest choice to prevent "immortal" orphans.
        if(getppid() == 1) 
            _exit(1);

        mChildIndex = id;
        PROCESS_POOL_INFO("Child " << mChildIndex << " (" << getpid() << ") is running");
    }
    else if(childPID > 0)
    {
        // Parent Process
        close(pipeFds[1]);
        mActiveChildren.push_back({ childPID, pipeFds[0], id });
    }

    return true;
}

// Spawns N children but keeps no more than M running at once
bool ProcessPool::Create(int procCount, int maxConcurrentProcs/*=0*/)
{
    assert(IsParent());
    assert(mActiveChildren.empty());

    if(maxConcurrentProcs == 0)
        maxConcurrentProcs = procCount;

    int maxChildCount = std::min(procCount, maxConcurrentProcs);

    PROCESS_POOL_INFO("Forking " << procCount << " children processes using "
                      << maxChildCount << " processors in parallel");

    // Pre-fork notification - for profiling, etc.
    OnNotify(NOTIFY_TYPE::PRE_FORK);

    bool result = true;
    for(int i = 0; i < procCount; ++i)
    {
        // Throttling: if we hit the limit, wait until at least one worker slot is free
        while((int)mActiveChildren.size() >= maxConcurrentProcs)
        {
            PROCESS_POOL_INFO("Limit reached (" << maxConcurrentProcs << "). Waiting for slot...");

            // If a child crashes while we are waiting, stop spawning entirely
            if(!CheckForFinishedChildren(1000))
            {
                PROCESS_POOL_ERROR("Aborting pool creation due to child process failure.");
                result = false;
                break;            
            }
        }

        // Fork a child
        if(!ForkChild(i))
        {
            result = false;
            break;            
        }

        // If we are child process then just continue running
        if(IsChild())
            return true; 

        // Child forking notification - for profiling, etc.
        OnNotify(NOTIFY_TYPE::CHILD_FORK);
    }

    // We must be parent if we are here
    assert(IsParent());

    if(!result)
    {
        // Something went wrong
        TerminateAll(); // Terminate children we've started
        return false;
    }

    // Post-fork notification - for profiling, etc.
    OnNotify(NOTIFY_TYPE::POST_FORK);

    // Wait for all children to complete (or any child crashed)
    PROCESS_POOL_INFO("Waiting for children processes to complete...");

    // Blocks until all active children are finished
    while(!mActiveChildren.empty())
    {
        if(!CheckForFinishedChildren(1000 /*ms*/))
        {
            result = false;
            break;
        }
    }

    if(result)
    {
        // At this point all children are either exited or alive but idle.
        PROCESS_POOL_INFO("All children completed");

        // Send "All children done" notification - for profiling, clean up, etc.
        OnNotify(NOTIFY_TYPE::CHILDREN_DONE);
    }

    return result;
}

// Exit completed child
inline void ProcessPool::Exit(int exitCode)
{
    if(IsParent())
    {
        PROCESS_POOL_ERROR("This method is not allowed in the parent process");
        return;
    }

    // Flush all open streams to make sure we don't miss any output
    fflush(nullptr);

    if(exitCode != 0)
    {
        PROCESS_POOL_ERROR("Child " << mChildIndex << " (" << getpid() << ") has failed"
                           " with exit code " << exitCode);
    }

    // Report clean exit status back to parent
    ChildStatus status = { exitCode, true };
    write(mWriteFd, &status, sizeof(status));
    close(mWriteFd);
    _exit(exitCode);
}

// Internal helper to close handles and attempt to reap zombies
inline void ProcessPool::CleanupChild(ChildInfo& child)
{
    if(child.readFd != -1)
    {
        close(child.readFd);
        child.readFd = -1;
    }

    if(child.pid > 0)
    {
        // Best effort reap using waitid; prevents system zombie exhaustion.
        // Use WEXITED | WNOHANG to check for termination without blocking
        // Note: We don't need to provide a pointer to siginfo_t if we don't
        // care about the details, but passing it is safer for error checking.
        siginfo_t info{};
        int result = waitid(P_PID, (id_t)child.pid, &info, WEXITED | WNOHANG);

        if(result == -1 && errno == ECHILD)
        {
            // SIGCHLD is ignored; kernel auto-reaped. This is "fine" for us
            // because we got the real status from the pipe earlier.
        }
        child.pid = -1;
    }
}

// Proactively checks for finished children using poll().
// Returns true if a child failed or crashed.
inline bool ProcessPool::CheckForFinishedChildren(int timeoutMs)
{
    if(mActiveChildren.empty())
        return true;

    std::vector<struct pollfd> pollFds;
    for(const auto& child : mActiveChildren)
    {
        pollFds.push_back({ child.readFd, POLLIN | POLLHUP, 0 });
    }

    // poll() wakes up instantly if a child dies (pipe closes) or data arrives
    // Handle interrupted system calls
    int pollResult = 0;
    do{
        pollResult = poll(pollFds.data(), pollFds.size(), timeoutMs);
    } while(pollResult == -1 && errno == EINTR);

    // Explicit handling fatal errors
    if(pollResult < 0)
    {
        // A truly fatal error occurred (ENOMEM, EINVAL, etc.)
        std::string errmsg = strerror(errno);
        PROCESS_POOL_ERROR("Parent " << getpid() << " fatal poll() error: " << errmsg);
        TerminateAll(); // Clean up everything
        return false;
    }

    // Handle Timeouts
    if(pollResult == 0) 
        return true;

    // Handle Events (pollResult > 0)    
    bool poolHealthy = true;

    // Iterate backwards to allow safe removal from the vector while looping
    for(int i = (int)mActiveChildren.size() - 1; i >= 0; --i)
    {
        if(pollFds[i].revents & (POLLIN | POLLHUP))
        {
            ChildStatus ws = { -1, false };

            // Handle interrupted system calls
            ssize_t bytesRead = 0;
            do{
                bytesRead = read(mActiveChildren[i].readFd, &ws, sizeof(ws));
            } while(bytesRead == -1 && errno == EINTR);

            if(bytesRead == (ssize_t)sizeof(ws)) 
            {
                // We got a status report!
                if(ws.exitCode != 0)
                {
                    PROCESS_POOL_ERROR("Child " << mActiveChildren[i].id << " (" << mActiveChildren[i].pid << ") " << 
                                       "reported failure exit code " << ws.exitCode << ".");
                    poolHealthy = false;
                }
                else
                {
                    PROCESS_POOL_INFO("Child " << mActiveChildren[i].id << " (" << mActiveChildren[i].pid << ") " << 
                                      "finished successfully.");
                }
            }
            else 
            {
                // Pipe closed (bytesRead == 0) or incomplete data.
                // This indicates a crash (SIGSEGV), a SIGKILL, or an abort.
                PROCESS_POOL_ERROR("Child " << mActiveChildren[i].id << " (" << mActiveChildren[i].pid << ") " << 
                                   "died unexpectedly (crashed or terminated) without status.");
                poolHealthy = false;
            }

            // We need to be aggressive about cleanup because once poll() notifies
            // us that a pipe is ready for reading or has been closed, that child’s
            // "life cycle" in our pool is effectively over. 
            // ALWAYS cleanup and remove if we got a poll event for this child.
            CleanupChild(mActiveChildren[i]);
            mActiveChildren.erase(mActiveChildren.begin() + i);

            if(!poolHealthy)
                break; // Stop checking others; trigger pool shutdown
        }
    }

    // If any child failed, trigger a global shutdown
    if(!poolHealthy)
    {
        TerminateAll();
        return false;
    }

    return true;
}

//
// TaskPool: A specialized ProcessPool_old for functional task execution.
// This class extends the base ProcessPool_old to allow passing a callable 
// (lambda, function pointer, or functor) as the child process entry point.
//
class TaskPool : public ProcessPool
{
public:
    TaskPool() = default;
    virtual ~TaskPool() = default;

    // Bring the base class Create methods into this scope to avoid "Name Hiding"
    using ProcessPool::Create;

    //
    // Overloaded Create that accepts a task.
    //
    // The 'task' callable MUST accept a single integer argument and return 
    // an integer exit code (e.g., int task(int index)). 
    //   - Returning true maps to a successful process exit (0).
    //   - Returning false maps to a failure process exit (1).
    //
    // Any additional data required by the task should be captured 
    // by the lambda at the call site.
    //    
    template <typename Func>
    bool Create(Func task, int procCount, int maxConcurrentProcs = 0)
    {
        // Call the base class logic to fork and manage processes
        if(!ProcessPool::Create(procCount, maxConcurrentProcs))
            return false;

        // The base Create returns true for both Parent and Child.
        // We intercept the Child process here to run the assigned task.
        if(IsChild())
        {
            int exitCode = 0;
            try
            {
                // Run the user-provided function
                exitCode = task(GetChildIndex());
            }
            catch(...)
            {
                // If the user's task throws, exit child with failure status
                exitCode = 1;   
            }
            Exit(exitCode);
        }

        // In a healthy execution, the child process terminates inside Exit() via _exit().
        // We return IsParent() here as a final safety "gatekeeper." 
        // This ensures that even if a child process somehow bypasses the Exit() call 
        // (e.g., due to a logic error or refactor), it will return 'false' and 
        // won't accidentally continue into the parent's logic flow.
        return IsParent();
    }
};


#endif // _PROCESS_POOL_HPP_
