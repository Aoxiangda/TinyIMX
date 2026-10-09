#include "common/net/EventLoopThreadPool.h"

#include "common/net/EventLoop.h"
#include "common/net/EventLoopThread.h"

#include <memory>
#include <utility>
#include <vector>

namespace tinyimx {

    EventLoopThreadPool::EventLoopThreadPool(
            EventLoop* base_loop,
            std::size_t thread_count
        )
            : base_loop_(base_loop),
            thread_count_(thread_count) {
        }


    EventLoopThreadPool::~EventLoopThreadPool() = default;

    bool EventLoopThreadPool::Start() {
        if (started_) {
            return false;
        }

        if (base_loop_ == nullptr) {
            return false;
        }

        if (!base_loop_->IsValid()) {
            return false;
        }

        if (!base_loop_->IsInLoopThread()) {
            return false;
        }

        threads_.reserve(thread_count_);
        loops_.reserve(thread_count_);

        for (std::size_t i = 0;
            i < thread_count_;
            ++i) {
            auto loop_thread =
                std::make_unique<EventLoopThread>();

            EventLoop* loop =
                loop_thread->StartLoop();

            if (loop == nullptr) {
                loops_.clear();
                threads_.clear();
                next_ = 0;
                return false;
            }

            loops_.push_back(loop);

            threads_.push_back(
                std::move(loop_thread)
            );
        }

        started_ = true;
        next_ = 0;

        return true;
    }

    EventLoop* EventLoopThreadPool::GetNextLoop() {
        if (!started_) {
            return nullptr;
        }

        if (base_loop_ == nullptr) {
            return nullptr;
        }

        if (!base_loop_->IsInLoopThread()) {
            return nullptr;
        }

        if (loops_.empty()) {
            return base_loop_;
        }

        EventLoop* loop =
            loops_[next_];

        ++next_;

        if (next_ >= loops_.size()) {
            next_ = 0;
        }

        return loop;
    }

    std::vector<EventLoop*>
    EventLoopThreadPool::GetAllLoops() const {
        if (!started_ || base_loop_ == nullptr) {
            return {};
        }

        if (loops_.empty()) {
            return {base_loop_};
        }

        return loops_;
    }

    bool EventLoopThreadPool::Started() const noexcept {
        return started_;
    }

    std::size_t EventLoopThreadPool::ThreadCount() const noexcept {
        return thread_count_;
    }

}  // namespace tinyimx