#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace tinyimx {

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool final {
public:
    EventLoopThreadPool(
        EventLoop* base_loop,
        std::size_t thread_count
    );

    ~EventLoopThreadPool();

    EventLoopThreadPool(
        const EventLoopThreadPool&
    ) = delete;

    EventLoopThreadPool& operator=(
        const EventLoopThreadPool&
    ) = delete;

    EventLoopThreadPool(
        EventLoopThreadPool&&
    ) = delete;

    EventLoopThreadPool& operator=(
        EventLoopThreadPool&&
    ) = delete;

    bool Start();

    EventLoop* GetNextLoop();

    std::vector<EventLoop*>
    GetAllLoops() const;

    bool Started() const noexcept;

    std::size_t
    ThreadCount() const noexcept;

private:
    EventLoop* base_loop_{nullptr};

    const std::size_t
        thread_count_{0};

    bool started_{false};

    std::size_t next_{0};

    std::vector<
        std::unique_ptr<EventLoopThread>
    > threads_;

    std::vector<EventLoop*> loops_;
};

}  // namespace tinyimx