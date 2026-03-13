//
// example.cpp
//
#include <iostream>         // std::cout
#include <sys/syscall.h>    // __NR_gettid
#include <unistd.h>         // syscall()
#include "threadPool.hpp"

// Helper method to get thread id...for debugging, logging, etc.
static pid_t GetThreadId()
{
    static thread_local pid_t threadId = syscall(__NR_gettid);
    return threadId;
}

// Helper macro to synchronize writing to std::cout
static std::mutex sLogMutex;
#define TRACE(msg) \
{ \
     std::unique_lock<std::mutex> lock(sLogMutex); \
     std::cout << "[tid=" << GetThreadId() << "] " << msg << std::endl; \
}

// Lambda to test thread pool with lambda expression
auto func1 = [](int count)
{
    // Do something here...
    usleep((random() % 5) * 1000); // Add a random 0-4 ms delay
    TRACE("count=" << count);
};

// Function to test thread pool with function pointer
void func2(int count, const std::string& str)
{
    // Do something here...
    usleep((random() % 5) * 1000); // Add a random 0-4 ms delay
    TRACE("count=" << count << ", str='" << str << "'");
};

// Class to test thread pool with pointer to member function
class MyClass
{
public:
    void func3(int count)
    {
        // Do something here...
        usleep((random() % 5) * 1000); // Add a random 0-4 ms delay
        TRACE("count=" << count << ", name='" << name << "'");
    }
    std::string name{"MyClass"};
};

void TestThreadPool()
{
    ThreadPool tpool;
    tpool.Start(4); // 4 threads

    // Test thread pool with lambda expression (func1)
    std::cout << ">>> Begin Of ThreadPool 'lambda exression' test" << std::endl;
    for(int i = 0; i < 20; i++)
    {
        tpool.Post(func1, i);
    }
    tpool.Wait();
    std::cout << ">>> End Of ThreadPool 'lambda exression' test" << std::endl;

    // Test thread pool with function pointer (func2)
    std::cout << ">>> Begin Of ThreadPool 'function pointer' test" << std::endl;
    for(int i = 0; i < 20; i++)
    {
        tpool.Post(func2, i, "hello" + std::to_string(i));
    }
    tpool.Wait();
    std::cout << ">>> End Of ThreadPool 'function pointer' test" << std::endl;

    // Test thread pool with pointer to member function (MyClass::func3)
    std::cout << ">>> Begin Of ThreadPool 'pointer to member function' test" << std::endl;
    MyClass obj;
    for(int i = 0; i < 20; i++)
    {
        tpool.Post(&MyClass::func3, &obj, i);
    }
    tpool.Wait();
    std::cout << ">>> End Of ThreadPool 'pointer to member function' test" << std::endl;
}

void TestThreadPoolEx()
{
    // Test thread pool with lambda expression (func1)
    {
        ThreadPoolEx tpool(func1);
        tpool.Start(4); // 4 threads

        std::cout << ">>> Begin Of ThreadPoolEx 'lambda exression' test" << std::endl;
        for(int i = 0; i < 20; i++)
        {
            tpool.Post(i);
        }
        tpool.Wait();
        std::cout << ">>> End Of ThreadPoolEx 'lambda exression' test" << std::endl;
    }

    // Test thread pool with function pointer (func2)
    {
        ThreadPoolEx tpool(func2);
        tpool.Start(4); // 4 threads

        std::cout << ">>> Begin Of ThreadPoolEx 'function pointer' test" << std::endl;
        for(int i = 0; i < 20; i++)
        {
            tpool.Post(i, "hello" + std::to_string(i));
        }
        tpool.Wait();
        std::cout << ">>> End Of ThreadPoolEx 'function pointer' test" << std::endl;
    }

    // Test thread pool with pointer to member function (MyClass::func3)
    {
        ThreadPoolEx tpool(&MyClass::func3);
        tpool.Start(4); // 4 threads

        std::cout << ">>> Begin Of ThreadPool 'pointer to member function' test" << std::endl;
        MyClass obj;
        for(int i = 0; i < 20; i++)
        {
            tpool.Post(&obj, i);
        }
        tpool.Wait();
        std::cout << ">>> End Of ThreadPool 'pointer to member function' test" << std::endl;
    }
}

int main()
{
    TestThreadPool();
    TestThreadPoolEx();
    return 0;
}

