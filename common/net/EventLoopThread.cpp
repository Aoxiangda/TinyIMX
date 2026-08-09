#include "common/net/EventLoopThread.h"

#include "common/net/EventLoop.h"

namespace tinyimx {

EventLoopThread::~EventLoopThread() {
    if (!thread_.joinable()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(
            mutex_
        );

        if (loop_ != nullptr) {
            loop_->Quit();
        }
    }

    thread_.join();
}

EventLoop* EventLoopThread::StartLoop() {
    if (thread_.joinable()) {
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(
            mutex_
        );

        loop_ = nullptr;
        ready_ = false;
    }

    thread_ = std::thread(
        &EventLoopThread::ThreadFunc,
        this
    );

    std::unique_lock<std::mutex> lock(
        mutex_
    );

    condition_.wait(
        lock,
        [this]() {
            return ready_;
        }
    );

    return loop_;
}

void EventLoopThread::ThreadFunc() {
    EventLoop loop;

    const bool valid =
        loop.IsValid();

    {
        std::lock_guard<std::mutex> lock(
            mutex_
        );

        if (valid) {
            loop_ = &loop;
        }

        ready_ = true;
    }

    condition_.notify_one();

    if (!valid) {
        return;
    }

    loop.Loop();

    {
        std::lock_guard<std::mutex> lock(
            mutex_
        );

        loop_ = nullptr;
    }
}

}  // namespace tinyimx