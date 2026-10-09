#pragma once

#include "common/cache/RedisConnection.h"
#include "common/config/ConfigTypes.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>

namespace tinyimx {

class RedisConnectionPool;

class RedisConnectionLease {
public:
    RedisConnectionLease() = default;

    RedisConnectionLease(
        RedisConnectionPool* pool,
        std::unique_ptr<RedisConnection> connection
    );

    ~RedisConnectionLease();

    RedisConnectionLease(const RedisConnectionLease&) = delete;
    RedisConnectionLease& operator=(const RedisConnectionLease&) = delete;

    RedisConnectionLease(RedisConnectionLease&& other) noexcept;
    RedisConnectionLease& operator=(
        RedisConnectionLease&& other
    ) noexcept;

    RedisConnection* operator->();
    const RedisConnection* operator->() const;

    RedisConnection& operator*();
    const RedisConnection& operator*() const;

    bool IsValid() const;
    explicit operator bool() const;

    void Reset();

private:
    RedisConnectionPool* pool_{nullptr};
    std::unique_ptr<RedisConnection> connection_;
};

class RedisConnectionPool {
public:
    RedisConnectionPool() = default;
    ~RedisConnectionPool();

    RedisConnectionPool(const RedisConnectionPool&) = delete;
    RedisConnectionPool& operator=(const RedisConnectionPool&) = delete;

    bool Initialize(const RedisConfig& config);

    void Shutdown();

    RedisConnectionLease Acquire(
        std::chrono::milliseconds timeout =
            std::chrono::milliseconds(3000)
    );

    std::size_t Size() const;
    std::size_t AvailableCount() const;

private:
    friend class RedisConnectionLease;

    void Release(std::unique_ptr<RedisConnection> connection);

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;

    RedisConfig config_;

    bool initialized_{false};
    bool shutting_down_{false};

    std::size_t size_{0};

    std::deque<std::unique_ptr<RedisConnection>> connections_;
};

}  // namespace tinyimx