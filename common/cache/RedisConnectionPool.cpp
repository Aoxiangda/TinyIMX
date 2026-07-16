#include "common/cache/RedisConnectionPool.h"

#include "common/logging/LogMacros.h"

#include <utility>

namespace tinyimx {

RedisConnectionLease::RedisConnectionLease(
    RedisConnectionPool* pool,
    std::unique_ptr<RedisConnection> connection
)
    : pool_(pool),
      connection_(std::move(connection)) {}

RedisConnectionLease::~RedisConnectionLease() {
    Reset();
}

RedisConnectionLease::RedisConnectionLease(
    RedisConnectionLease&& other
) noexcept
    : pool_(other.pool_),
      connection_(std::move(other.connection_)) {
    other.pool_ = nullptr;
}

RedisConnectionLease& RedisConnectionLease::operator=(
    RedisConnectionLease&& other
) noexcept {
    if (this == &other) {
        return *this;
    }

    Reset();

    pool_ = other.pool_;
    connection_ = std::move(other.connection_);
    other.pool_ = nullptr;

    return *this;
}

RedisConnection* RedisConnectionLease::operator->() {
    return connection_.get();
}

const RedisConnection* RedisConnectionLease::operator->() const {
    return connection_.get();
}

RedisConnection& RedisConnectionLease::operator*() {
    return *connection_;
}

const RedisConnection& RedisConnectionLease::operator*() const {
    return *connection_;
}

bool RedisConnectionLease::IsValid() const {
    return connection_ != nullptr;
}

RedisConnectionLease::operator bool() const {
    return IsValid();
}

void RedisConnectionLease::Reset() {
    if (connection_ == nullptr) {
        pool_ = nullptr;
        return;
    }

    if (pool_ != nullptr) {
        pool_->Release(std::move(connection_));
    } else {
        connection_.reset();
    }

    pool_ = nullptr;
}

RedisConnectionPool::~RedisConnectionPool() {
    Shutdown();
}

bool RedisConnectionPool::Initialize(
    const RedisConfig& config
) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        LOG_WARN("redis connection pool already initialized");
        return true;
    }

    if (!config.enable) {
        LOG_ERROR("redis connection pool initialize failed: redis disabled");
        return false;
    }

    if (config.pool_size <= 0) {
        LOG_ERROR("redis connection pool initialize failed: invalid pool_size="
                  << config.pool_size);
        return false;
    }

    config_ = config;
    shutting_down_ = false;

    for (int i = 0; i < config.pool_size; ++i) {
        auto connection = std::make_unique<RedisConnection>();

        if (!connection->Connect(config_)) {
            LOG_ERROR("redis connection pool create connection failed"
                      << ", index=" << i
                      << ", error=" << connection->LastError());
            connections_.clear();
            size_ = 0;
            initialized_ = false;
            return false;
        }

        connections_.push_back(std::move(connection));
    }

    size_ = connections_.size();
    initialized_ = true;

    LOG_INFO("redis connection pool initialized"
             << ", size=" << size_
             << ", host=" << config_.host
             << ", port=" << config_.port
             << ", db=" << config_.db);

    return true;
}

void RedisConnectionPool::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ && connections_.empty()) {
        return;
    }

    shutting_down_ = true;

    connections_.clear();
    size_ = 0;
    initialized_ = false;

    cv_.notify_all();

    LOG_INFO("redis connection pool shutdown");
}

RedisConnectionLease RedisConnectionPool::Acquire(
    std::chrono::milliseconds timeout
) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (!initialized_ || shutting_down_) {
        LOG_ERROR("redis connection pool acquire failed: pool not available");
        return {};
    }

    const bool ready = cv_.wait_for(lock, timeout, [this]() {
        return shutting_down_ || !connections_.empty();
    });

    if (!ready || shutting_down_) {
        LOG_ERROR("redis connection pool acquire timeout");
        return {};
    }

    auto connection = std::move(connections_.front());
    connections_.pop_front();

    lock.unlock();

    if (!connection->Ping()) {
        LOG_WARN("redis connection ping failed, reconnecting"
                 << ", error=" << connection->LastError());

        if (!connection->Connect(config_)) {
            LOG_ERROR("redis connection reconnect failed"
                      << ", error=" << connection->LastError());
            return {};
        }
    }

    return RedisConnectionLease(this, std::move(connection));
}

std::size_t RedisConnectionPool::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}

std::size_t RedisConnectionPool::AvailableCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}

void RedisConnectionPool::Release(
    std::unique_ptr<RedisConnection> connection
) {
    if (connection == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (shutting_down_) {
        return;
    }

    connections_.push_back(std::move(connection));
    cv_.notify_one();
}

}  // namespace tinyimx