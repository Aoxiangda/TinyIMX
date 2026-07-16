#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace tinyimx {

template <typename T>
class CircularQueue {
public:
    explicit CircularQueue(std::size_t capacity)
        : buffer_(capacity + 1),
          capacity_(capacity + 1) {}

    CircularQueue(const CircularQueue&) = delete;
    CircularQueue& operator=(const CircularQueue&) = delete;

    bool PushBack(T item) {
        if (Full()) {
            return false;
        }

        buffer_[tail_] = std::move(item);
        tail_ = Next(tail_);
        return true;
    }

    bool PopFront(T* item) {
        if (item == nullptr || Empty()) {
            return false;
        }

        const std::size_t old_head = head_;
        *item = std::move(buffer_[old_head]);
        buffer_[old_head] = T{};

        head_ = Next(head_);
        return true;
    }

    bool DropFront() {
        if (Empty()) {
            return false;
        }

        const std::size_t old_head = head_;
        buffer_[old_head] = T{};

        head_ = Next(head_);
        ++overrun_counter_;
        return true;
    }

    bool Empty() const {
        return head_ == tail_;
    }

    bool Full() const {
        return Next(tail_) == head_;
    }

    std::size_t Size() const {
        if (tail_ >= head_) {
            return tail_ - head_;
        }

        return capacity_ - head_ + tail_;
    }

    std::size_t Capacity() const {
        return capacity_ - 1;
    }

    std::size_t OverrunCounter() const {
        return overrun_counter_;
    }

    void Clear(bool count_overrun) {
        while (!Empty()) {
            if (count_overrun) {
                DropFront();
                continue;
            }

            const std::size_t old_head = head_;
            buffer_[old_head] = T{};
            head_ = Next(head_);
        }
    }

    void Clear() {
        Clear(false);
    }


private:
    std::size_t Next(std::size_t index) const {
        return (index + 1) % capacity_;
    }

private:
    std::vector<T> buffer_;
    std::size_t capacity_{0};
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t overrun_counter_{0};
};

}  // namespace tinyimx