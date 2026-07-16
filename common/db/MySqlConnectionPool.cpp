#include "common/db/MySqlConnectionPool.h"

#include "common/logging/LogMacros.h"

#include <utility>

namespace tinyimx {

MySqlConnectionLease::MySqlConnectionLease(
    MySqlConnectionPool* pool,
    std::unique_ptr<MySqlConnection> connection
)
    : pool_(pool),
      connection_(std::move(connection)) {}

MySqlConnectionLease::~MySqlConnectionLease() {
    Reset();
}

MySqlConnectionLease::MySqlConnectionLease(
    MySqlConnectionLease&& other
) noexcept
    : pool_(other.pool_),
      connection_(std::move(other.connection_)) {
    other.pool_ = nullptr;
}

MySqlConnectionLease& MySqlConnectionLease::operator=(
    MySqlConnectionLease&& other
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

MySqlConnection* MySqlConnectionLease::operator->() {
    return connection_.get();
}

const MySqlConnection* MySqlConnectionLease::operator->() const {
    return connection_.get();
}

MySqlConnection& MySqlConnectionLease::operator*() {
    return *connection_;
}

const MySqlConnection& MySqlConnectionLease::operator*() const {
    return *connection_;
}

bool MySqlConnectionLease::IsValid() const {
    return connection_ != nullptr;
}

MySqlConnectionLease::operator bool() const {
    return IsValid();
}

void MySqlConnectionLease::Reset() {
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

MySqlConnectionPool::~MySqlConnectionPool() {
    Shutdown();
}

bool MySqlConnectionPool::Initialize(
    const MySqlConfig& config
) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        LOG_WARN("mysql connection pool already initialized");
        return true;
    }

    if (!config.enable) {
        LOG_ERROR("mysql connection pool initialize failed: mysql disabled");
        return false;
    }

    if (config.pool_size <= 0) {
        LOG_ERROR("mysql connection pool initialize failed: invalid pool_size="
                  << config.pool_size);
        return false;
    }

    config_ = config;
    shutting_down_ = false;

    for (int i = 0; i < config.pool_size; ++i) {
        auto connection = std::make_unique<MySqlConnection>();

        if (!connection->Connect(config_)) {
            LOG_ERROR("mysql connection pool create connection failed"
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

    LOG_INFO("mysql connection pool initialized"
             << ", size=" << size_
             << ", host=" << config_.host
             << ", database=" << config_.database
             << ", user=" << config_.user);

    return true;
}

void MySqlConnectionPool::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ && connections_.empty()) {
        return;
    }

    shutting_down_ = true;

    connections_.clear();
    size_ = 0;
    initialized_ = false;

    cv_.notify_all();

    LOG_INFO("mysql connection pool shutdown");
}

MySqlConnectionLease MySqlConnectionPool::Acquire(
    std::chrono::milliseconds timeout
) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (!initialized_ || shutting_down_) {
        LOG_ERROR("mysql connection pool acquire failed: pool not available");
        return {};
    }

    const bool ready = cv_.wait_for(lock, timeout, [this]() {
        return shutting_down_ || !connections_.empty();
    });

    if (!ready || shutting_down_) {
        LOG_ERROR("mysql connection pool acquire timeout");
        return {};
    }

    auto connection = std::move(connections_.front());
    connections_.pop_front();

    lock.unlock();

    if (!connection->Ping()) {
        LOG_WARN("mysql connection ping failed, reconnecting"
                 << ", error=" << connection->LastError());

        if (!connection->Connect(config_)) {
            LOG_ERROR("mysql connection reconnect failed"
                      << ", error=" << connection->LastError());
            return {};
        }
    }

    return MySqlConnectionLease(this, std::move(connection));
}

std::size_t MySqlConnectionPool::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}

std::size_t MySqlConnectionPool::AvailableCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}

void MySqlConnectionPool::Release(
    std::unique_ptr<MySqlConnection> connection
) {
    if (connection == nullptr) {
        return;
    }

    if (connection->InTransaction()) {
        LOG_WARN(
            "mysql connection returned with active transaction, "
            "rolling back automatically"
        );

        if (!connection->Rollback()) {
            LOG_ERROR(
                "mysql automatic rollback before release failed"
                << ", error=" << connection->LastError()
            );

            connection->Close();
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (shutting_down_) {
        return;
    }

    connections_.push_back(std::move(connection));
    cv_.notify_one();
}

}  // namespace tinyimx