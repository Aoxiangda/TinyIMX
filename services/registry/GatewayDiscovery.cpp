#include "services/registry/GatewayDiscovery.h"

#include "common/logging/LogMacros.h"

#include <algorithm>
#include <chrono>
#include <system_error>
#include <utility>

namespace tinyimx {

GatewayDiscovery::GatewayDiscovery(
    GatewayRegistry* registry,
    int refresh_interval_seconds
)
    : registry_(registry),
      refresh_interval_seconds_(
          refresh_interval_seconds
      ) {
}

GatewayDiscovery::~GatewayDiscovery() {
    Stop();
}

bool GatewayDiscovery::Start() {
    if (running_.load(
            std::memory_order_acquire
        )) {
        return true;
    }

    if (registry_ == nullptr) {
        LOG_ERROR(
            "gateway discovery start failed: "
            "registry is null"
        );

        return false;
    }

    if (refresh_interval_seconds_ <= 0) {
        LOG_ERROR(
            "gateway discovery start failed: "
            "invalid refresh interval"
        );

        return false;
    }

    /*
     * 启动时先同步拉取一次。
     *
     * 这样 Start() 成功以后，
     * 本地 snapshot 就已经可用了，
     * 而不是还要等第一次定时刷新。
     */
    if (!RefreshNow()) {
        LOG_ERROR(
            "gateway discovery initial "
            "refresh failed"
        );

        return false;
    }

    running_.store(
        true,
        std::memory_order_release
    );

    try {
        refresh_thread_ =
            std::thread(
                &GatewayDiscovery::RefreshLoop,
                this
            );
    } catch (
        const std::system_error& error
    ) {
        running_.store(
            false,
            std::memory_order_release
        );

        LOG_ERROR(
            "gateway discovery thread "
            "start failed"
            << ", error="
            << error.what()
        );

        return false;
    }

    LOG_INFO(
        "gateway discovery started"
        << ", refresh_interval_seconds="
        << refresh_interval_seconds_
        << ", instance_count="
        << Size()
    );

    return true;
}

void GatewayDiscovery::Stop() {
    if (!running_.exchange(
            false,
            std::memory_order_acq_rel
        )) {
        return;
    }

    wait_cv_.notify_all();

    if (
        refresh_thread_.joinable() &&
        refresh_thread_.get_id() !=
            std::this_thread::get_id()
    ) {
        refresh_thread_.join();
    }

    LOG_INFO(
        "gateway discovery stopped"
    );
}

bool GatewayDiscovery::RefreshNow() {
    if (registry_ == nullptr) {
        return false;
    }

    const auto result =
        registry_->ListActiveGateways();

    if (!result.Succeeded()) {
        LOG_WARN(
            "gateway discovery refresh failed"
            << ", error="
            << result.error_message
        );

        return false;
    }

    std::unordered_map<
        std::string,
        GatewayInstanceRecord
    > new_instances;

    new_instances.reserve(
        result.instances.size()
    );

    for (const auto& instance :
         result.instances) {
        new_instances.insert_or_assign(
            instance.gateway_id,
            instance
        );
    }

    {
        std::unique_lock<
            std::shared_mutex
        > lock(snapshot_mutex_);

        instances_.swap(
            new_instances
        );
    }

    LOG_INFO(
        "gateway discovery snapshot refreshed"
        << ", instance_count="
        << result.instances.size()
    );

    return true;
}

bool GatewayDiscovery::IsRunning()
    const noexcept {
    return running_.load(
        std::memory_order_acquire
    );
}

std::size_t GatewayDiscovery::Size()
    const {
    std::shared_lock<
        std::shared_mutex
    > lock(snapshot_mutex_);

    return instances_.size();
}

std::vector<GatewayInstanceRecord>
GatewayDiscovery::Snapshot() const {
    std::vector<GatewayInstanceRecord>
        snapshot;

    {
        std::shared_lock<
            std::shared_mutex
        > lock(snapshot_mutex_);

        snapshot.reserve(
            instances_.size()
        );

        for (const auto& item :
             instances_) {
            snapshot.push_back(
                item.second
            );
        }
    }

    std::sort(
        snapshot.begin(),
        snapshot.end(),
        [](
            const GatewayInstanceRecord& lhs,
            const GatewayInstanceRecord& rhs
        ) {
            return lhs.gateway_id <
                   rhs.gateway_id;
        }
    );

    return snapshot;
}

std::optional<GatewayInstanceRecord>
GatewayDiscovery::FindById(
    const std::string& gateway_id
) const {
    if (gateway_id.empty()) {
        return std::nullopt;
    }

    std::shared_lock<
        std::shared_mutex
    > lock(snapshot_mutex_);

    const auto iterator =
        instances_.find(gateway_id);

    if (iterator == instances_.end()) {
        return std::nullopt;
    }

    return iterator->second;
}

void GatewayDiscovery::RefreshLoop() {
    const auto interval =
        std::chrono::seconds(
            refresh_interval_seconds_
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

        /*
         * 刷新失败时不清空旧 snapshot。
         *
         * Redis 短暂故障时，
         * 保留上一次成功数据。
         */
        RefreshNow();

        lock.lock();
    }
}

}  // namespace tinyimx