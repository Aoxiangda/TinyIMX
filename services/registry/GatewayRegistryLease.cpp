#include "services/registry/GatewayRegistryLease.h"

#include "common/logging/LogMacros.h"

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace tinyimx {

GatewayRegistryLease::GatewayRegistryLease(
    GatewayRegistry* registry,
    GatewayRegistryLeaseOptions options,
    LeaseLostCallback lease_lost_callback
)
    : registry_(registry),
      options_(std::move(options)),
      lease_lost_callback_(
          std::move(
              lease_lost_callback
          )
      ) {
}

GatewayRegistryLease::~GatewayRegistryLease() {
    Stop();
}

bool GatewayRegistryLease::Start() {
    if (running_.load(
            std::memory_order_acquire
        )) {
        return true;
    }

    if (registry_ == nullptr) {
        LOG_ERROR(
            "gateway registry lease start failed: "
            "registry is null"
        );

        return false;
    }

    if (options_.gateway_id.empty()) {
        LOG_ERROR(
            "gateway registry lease start failed: "
            "gateway_id is empty"
        );

        return false;
    }

    if (options_.advertise_host.empty()) {
        LOG_ERROR(
            "gateway registry lease start failed: "
            "advertise_host is empty"
        );

        return false;
    }

    if (options_.advertise_port == 0) {
        LOG_ERROR(
            "gateway registry lease start failed: "
            "advertise_port is zero"
        );

        return false;
    }

    if (
        options_.lease_ttl_seconds <= 0 ||
        options_.
            heartbeat_interval_seconds <= 0
    ) {
        LOG_ERROR(
            "gateway registry lease start failed: "
            "invalid ttl or heartbeat interval"
        );

        return false;
    }

    record_.gateway_id =
        options_.gateway_id;

    record_.lease_token =
        GenerateLeaseToken();

    record_.listen_host =
        options_.advertise_host;

    record_.listen_port =
        options_.advertise_port;

    record_.started_at =
        UnixNowSeconds();

    const auto register_result =
        registry_->Register(
            record_,
            options_.lease_ttl_seconds
        );

    if (!register_result.Succeeded()) {
        LOG_ERROR(
            "gateway registry lease register failed"
            << ", gateway_id="
            << options_.gateway_id
            << ", status="
            << RegisterGatewayStatusToString(
                register_result.status
            )
            << ", error="
            << register_result.error_message
        );

        return false;
    }

    owns_lease_.store(
        true,
        std::memory_order_release
    );

    running_.store(
        true,
        std::memory_order_release
    );

    try {
        heartbeat_thread_ =
            std::thread(
                &GatewayRegistryLease::
                    HeartbeatLoop,
                this
            );
    } catch (
        const std::system_error& error
    ) {
        running_.store(
            false,
            std::memory_order_release
        );

        owns_lease_.store(
            false,
            std::memory_order_release
        );

        registry_->UnregisterIfMatch(
            record_.gateway_id,
            record_.lease_token
        );

        LOG_ERROR(
            "gateway registry heartbeat "
            "thread start failed"
            << ", gateway_id="
            << options_.gateway_id
            << ", error="
            << error.what()
        );

        return false;
    }

    LOG_INFO(
        "gateway registry lease started"
        << ", gateway_id="
        << options_.gateway_id
        << ", advertise="
        << options_.advertise_host
        << ':'
        << options_.advertise_port
        << ", ttl_seconds="
        << options_.lease_ttl_seconds
        << ", heartbeat_interval_seconds="
        << options_.
            heartbeat_interval_seconds
    );

    return true;
}

void GatewayRegistryLease::Stop() {
    running_.store(
        false,
        std::memory_order_release
    );

    wait_cv_.notify_all();

    if (
        heartbeat_thread_.joinable() &&
        heartbeat_thread_.get_id() !=
            std::this_thread::get_id()
    ) {
        heartbeat_thread_.join();
    }

    if (!owns_lease_.exchange(
            false,
            std::memory_order_acq_rel
        )) {
        return;
    }

    if (registry_ == nullptr) {
        return;
    }

    const auto result =
        registry_->UnregisterIfMatch(
            record_.gateway_id,
            record_.lease_token
        );

    if (!result.Completed()) {
        LOG_WARN(
            "gateway registry lease "
            "unregister failed"
            << ", gateway_id="
            << record_.gateway_id
            << ", status="
            << UnregisterGatewayStatusToString(
                result.status
            )
            << ", error="
            << result.error_message
        );

        return;
    }

    LOG_INFO(
        "gateway registry lease stopped"
        << ", gateway_id="
        << record_.gateway_id
        << ", status="
        << UnregisterGatewayStatusToString(
            result.status
        )
    );
}

bool GatewayRegistryLease::IsRunning()
    const noexcept {
    return running_.load(
        std::memory_order_acquire
    );
}

const std::string&
GatewayRegistryLease::LeaseToken()
    const noexcept {
    return record_.lease_token;
}

void GatewayRegistryLease::HeartbeatLoop() {
    const auto interval =
        std::chrono::seconds(
            options_.
                heartbeat_interval_seconds
        );

    std::unique_lock<std::mutex>
        lock(wait_mutex_);

    while (
        running_.load(
            std::memory_order_acquire
        )
    ) {
        const bool stopped =
            wait_cv_.wait_for(
                lock,
                interval,
                [this]() {
                    return !running_.load(
                        std::memory_order_acquire
                    );
                }
            );

        if (stopped) {
            break;
        }

        lock.unlock();

        RefreshOnce();

        lock.lock();
    }
}

void GatewayRegistryLease::RefreshOnce() {
    if (
        registry_ == nullptr ||
        !running_.load(
            std::memory_order_acquire
        )
    ) {
        return;
    }

    const auto result =
        registry_->RefreshIfMatch(
            record_.gateway_id,
            record_.lease_token,
            options_.lease_ttl_seconds
        );

    switch (result.status) {
        case RefreshGatewayLeaseStatus::
            kRefreshed:
            return;

        case RefreshGatewayLeaseStatus::
            kRedisError:

            LOG_WARN(
                "gateway registry lease "
                "refresh redis error"
                << ", gateway_id="
                << record_.gateway_id
                << ", error="
                << result.error_message
            );

            return;

        case RefreshGatewayLeaseStatus::
            kNotFound:

            LOG_WARN(
                "gateway registry lease missing, "
                "trying to register again"
                << ", gateway_id="
                << record_.gateway_id
            );

            owns_lease_.store(
                false,
                std::memory_order_release
            );

            RecoverMissingLease();

            return;

        case RefreshGatewayLeaseStatus::
            kMismatch:

            MarkLeaseLost(
                "lease token mismatch"
            );

            return;

        case RefreshGatewayLeaseStatus::
            kInvalidRecord:

            MarkLeaseLost(
                "registry record is invalid"
            );

            return;

        case RefreshGatewayLeaseStatus::
            kInvalidArgument:

            MarkLeaseLost(
                "invalid refresh argument"
            );

            return;
    }
}

void GatewayRegistryLease::
RecoverMissingLease() {
    const auto result =
        registry_->Register(
            record_,
            options_.lease_ttl_seconds
        );

    if (result.Succeeded()) {
        owns_lease_.store(
            true,
            std::memory_order_release
        );

        LOG_WARN(
            "gateway registry lease recovered"
            << ", gateway_id="
            << record_.gateway_id
        );

        return;
    }

    MarkLeaseLost(
        std::string(
            "lease recovery failed: "
        ) +
        RegisterGatewayStatusToString(
            result.status
        ) +
        ", error=" +
        result.error_message
    );
}

void GatewayRegistryLease::MarkLeaseLost(
    const std::string& reason
) {
    owns_lease_.store(
        false,
        std::memory_order_release
    );

    running_.store(
        false,
        std::memory_order_release
    );

    wait_cv_.notify_all();

    LOG_ERROR(
        "gateway registry lease lost"
        << ", gateway_id="
        << record_.gateway_id
        << ", reason="
        << reason
    );

    if (lease_lost_callback_) {
        lease_lost_callback_();
    }
}

std::string
GatewayRegistryLease::GenerateLeaseToken() {
    std::random_device random_device;

    const auto now =
        static_cast<std::uint64_t>(
            std::chrono::steady_clock::
                now().
                time_since_epoch().
                count()
        );

    const auto pid =
        static_cast<std::uint64_t>(
            ::getpid()
        );

    const std::uint64_t high =
        (
            static_cast<std::uint64_t>(
                random_device()
            ) << 32
        ) ^
        static_cast<std::uint64_t>(
            random_device()
        ) ^
        now;

    const std::uint64_t low =
        (
            static_cast<std::uint64_t>(
                random_device()
            ) << 32
        ) ^
        static_cast<std::uint64_t>(
            random_device()
        ) ^
        pid ^
        (now << 1);

    std::ostringstream stream;

    stream
        << std::hex
        << std::setfill('0')
        << std::setw(16)
        << high
        << std::setw(16)
        << low;

    return stream.str();
}

std::int64_t
GatewayRegistryLease::UnixNowSeconds() {
    const auto now =
        std::chrono::system_clock::now();

    return
        std::chrono::duration_cast<
            std::chrono::seconds
        >(
            now.time_since_epoch()
        ).count();
}

}  // namespace tinyimx