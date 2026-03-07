//
// pipe.hpp
//
#ifndef __PIPE_HPP__
#define __PIPE_HPP__

#include <deque>
#include <mutex>
#include <condition_variable>

//
// Pipe class to provide thread-safe read/write access to stored objects
//
template<class DATA>
class Pipe
{
public:
    Pipe(size_t capacity = 0) : mCapacity(capacity) {}
    ~Pipe() { Clear(); }

    // Push a new data object to the pipe.
    // If the pipe reached its max capacity, defined by SetMaxPipeSize()
    // then wait until more room available.
    void Push(const DATA& data);
    void Push(DATA&& data);

    // Pop a next data object from the pipe, wait for it to arrive if necessary.
    // Return false once the pipe is empty AND no more data is expected (we are done).
    // Return true otherwise.
    bool Pop(DATA& data);

    // Indicate that no more data is expected.
    // This will cause Pop() to return false once last data object is popped.
    void SetHasMore(bool hasMore);

    // Empty the pipe and reset it to initial state.
    void Clear();

private:
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;

    std::deque<DATA> mDataList;
    size_t mCapacity = 0;
    bool mHasMore = true;   // Initially, since we expect data to be pushed
    std::mutex mMtx;
    std::condition_variable mPopCv;
    std::condition_variable mPushCv;
};

//
// Pipe class implementation
//
template<class DATA>
void Pipe<DATA>::Push(const DATA& data)
{
    std::unique_lock<std::mutex> lock(mMtx);

    // If capacity is unlimited (mCapacity == 0) OR there is available
    // room in the list, then stop executing the wait call (without blocking)
    // and proceed directly to insert the data.
    mPushCv.wait(lock, [this](){ return (mCapacity == 0 || mDataList.size() < mCapacity); });

    mDataList.emplace_back(data);
    mPopCv.notify_one();
}

template<class DATA>
void Pipe<DATA>::Push(DATA&& data)
{
    std::unique_lock<std::mutex> lock(mMtx);

    // If capacity is unlimited (mCapacity == 0) OR there is available
    // room in the list (mDataList.size() < mCapacity), then proceed
    // directly to insert the data.
    mPushCv.wait(lock, [this](){ return (mCapacity == 0 || mDataList.size() < mCapacity); });

    mDataList.emplace_back(std::move(data));
    mPopCv.notify_one();
}

template<class DATA>
bool Pipe<DATA>::Pop(DATA& data)
{
    // Wait for a data to arrive
    std::unique_lock<std::mutex> lock(mMtx);

    // If data list is empty and we don't expect more data, then return
    if(mDataList.empty() && !mHasMore)
        return false; // No more data

    // Block until one of the following conditions is met:
    // 1. The data list has an item to consume (!mDataList.empty())
    // 2. We don't expect more data to arrive (!mHasMore)
    mPopCv.wait(lock, [this] { return (!mDataList.empty() || !mHasMore); });

    // If data list is empty and we no longer expect more data, then we are done
    if(mDataList.empty() && !mHasMore)
        return false; // No more data

    // Data list has some data. Pop the front data item
    DATA& front = mDataList.front();
    data = std::move(front);
    mDataList.pop_front();

    // Notify Push() we might have more room in our list
    mPushCv.notify_one();
    return true;
}

template<class DATA>
void Pipe<DATA>::SetHasMore(bool hasMore)
{
    std::unique_lock<std::mutex> lock(mMtx);
    mHasMore = hasMore;
    mPopCv.notify_all();    // In case we are waiting in Pop()
}

template<class DATA>
void Pipe<DATA>::Clear()
{
    std::unique_lock<std::mutex> lock(mMtx);
    mDataList.clear();
    mHasMore = true;        // Resets to initial state
    mPopCv.notify_all();    // In case we are waiting in Pop()
    mPushCv.notify_all();   // In case we are waiting in Push()
}

#endif // __PIPE_HPP__

