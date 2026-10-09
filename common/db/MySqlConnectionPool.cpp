#include "common/db/MySqlConnectionPool.h"

#include "common/logging/LogMacros.h"
#include "common/db/StorageWaitTiming.h"
#include <cstdint>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <time.h>
#include <utility>

namespace tinyimx {
namespace {

using AcquireClock = std::chrono::steady_clock;

bool SampleAcquireTrace() noexcept {
    static const bool enabled = [] {
        const char* flag = std::getenv("TINYIMX_MYSQL_POOL_TRACE");
        return flag != nullptr && flag[0] == '1' && flag[1] == '\0';
    }();
    if (!enabled) return false;
    static std::atomic<std::int64_t> next_ns{0};
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        AcquireClock::now().time_since_epoch()).count();
    auto expected = next_ns.load(std::memory_order_relaxed);
    return now >= expected && next_ns.compare_exchange_strong(
        expected, now + 125000000, std::memory_order_relaxed);
}

std::int64_t AcquireThreadCpuNs() noexcept {
#if defined(__linux__)
    timespec value{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) == 0)
        return static_cast<std::int64_t>(value.tv_sec) * 1000000000 + value.tv_nsec;
#endif
    return -1;
}

// Created before the pool lock so its destructor logs after the lock releases,
// including early returns. A diagnostic failure must not change lease behavior.
class AcquirePhaseTrace {
public:
    AcquirePhaseTrace() noexcept : selected_(SampleAcquireTrace()) {
        if (selected_) {
            start_ = AcquireClock::now();
            cpu_start_ = AcquireThreadCpuNs(); thread_ = diagnostics::CurrentThreadId();
        }
    }
    ~AcquirePhaseTrace() noexcept {
        if (!selected_) return;
        const auto total = Micros(AcquireClock::now() - start_);
        const auto cpu_end = AcquireThreadCpuNs();
        const auto cpu = cpu_start_ >= 0 && cpu_end >= cpu_start_
            ? (cpu_end - cpu_start_) / 1000 : -1;
        try {
            LOG_WARN("mysql_pool_acquire_phase"
                << " status=" << status_ << " pool_size=" << size_
                << " free_slots_before=" << free_ << " tid=" << thread_ << " started_us=" << std::chrono::duration_cast<std::chrono::microseconds>(start_.time_since_epoch()).count() << " mutex_wait_us=" << mutex_wait_us_
                << " slot_wait_us=" << wait_us_ << " ping_us=" << ping_us_
                << " reconnect_us=" << reconnect_us_ << " total_us=" << total
                << " thread_cpu_us=" << cpu);
        } catch (...) {
            // Numeric observability must never throw into a business request.
        }
    }
    void Slots(std::size_t size, std::size_t free) noexcept {
        size_ = size; free_ = free;
    }
    void Outcome(int status) noexcept { status_ = status; }
    void MutexLocked() noexcept { if(selected_){mutex_wait_us_=Micros(AcquireClock::now()-start_);phase_=AcquireClock::now();} }
    void SlotReady() noexcept {
        if (selected_) wait_us_ = Micros(AcquireClock::now() - phase_);
    }
    void PhaseStart() noexcept { if (selected_) phase_ = AcquireClock::now(); }
    void PingDone() noexcept {
        if (selected_) ping_us_ = Micros(AcquireClock::now() - phase_);
    }
    void ReconnectDone() noexcept {
        if (selected_) reconnect_us_ = Micros(AcquireClock::now() - phase_);
    }
private:
    static std::int64_t Micros(AcquireClock::duration value) noexcept {
        return std::chrono::duration_cast<std::chrono::microseconds>(value).count();
    }
    bool selected_;
    int status_{0}; // 0=healthy/reconnected lease,1=unavailable,2=timeout,3=reconnect failure
    std::size_t size_{0}, free_{0}; std::uint64_t thread_{0}; std::int64_t mutex_wait_us_{0};
    AcquireClock::time_point start_{}, phase_{};
    std::int64_t cpu_start_{-1}, wait_us_{0}, ping_us_{0}, reconnect_us_{0};
};

} // namespace

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
    AcquirePhaseTrace trace;
    std::unique_lock<std::mutex> lock(mutex_);
    trace.MutexLocked();
    trace.Slots(size_, connections_.size());

    if (!initialized_ || shutting_down_) {
        trace.Outcome(1);
        trace.SlotReady();
        LOG_ERROR("mysql connection pool acquire failed: pool not available");
        return {};
    }

    const bool ready = cv_.wait_for(lock, timeout, [this]() {
        return shutting_down_ || !connections_.empty();
    });

    if (!ready || shutting_down_) {
        trace.Outcome(2);
        trace.SlotReady();
        LOG_ERROR("mysql connection pool acquire timeout");
        return {};
    }

    auto connection = std::move(connections_.front());
    connections_.pop_front();
    trace.SlotReady();

    lock.unlock();

    trace.PhaseStart();
    const bool ping_ok = connection->Ping();
    trace.PingDone();
    if (!ping_ok) {
        LOG_WARN("mysql connection ping failed, reconnecting"
                 << ", error=" << connection->LastError());

        trace.PhaseStart();
        const bool reconnected = connection->Connect(config_);
        trace.ReconnectDone();
        if (!reconnected) {
            trace.Outcome(3);
            LOG_ERROR("mysql connection reconnect failed"
                      << ", error=" << connection->LastError());
            // Keep the slot for a later retry after the dependency recovers.
            // Release also respects shutdown and transaction cleanup.
            Release(std::move(connection));
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
