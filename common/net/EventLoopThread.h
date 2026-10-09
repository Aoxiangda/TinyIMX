#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

namespace tinyimx {
    class EventLoop;

    class EventLoopThread final {
        public:
            EventLoopThread() = default;
            ~EventLoopThread();

            EventLoopThread(const EventLoopThread&) = delete;
            EventLoopThread& operator = (const EventLoopThread&) = delete;
            EventLoopThread(EventLoopThread&&) = delete;
            EventLoopThread& operator = (EventLoopThread&&) = delete;

            EventLoop* StartLoop();
        private:
            void ThreadFunc();

        private:
            std::thread thread_;

            std::mutex mutex_;
            std::condition_variable condition_;


            EventLoop* loop_{nullptr};

            bool ready_{false};
    };
}