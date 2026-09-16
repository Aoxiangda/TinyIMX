#include "services/registry/zookeeper/ZooKeeperServiceDiscovery.h"

#include "common/logging/LogMacros.h"
#include "services/registry/zookeeper/ZooKeeperTypes.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <set>
#include <unordered_set>
#include <utility>

namespace tinyimx::registry::zookeeper {
namespace {

constexpr auto kControlTick = std::chrono::milliseconds(200);
constexpr std::array<const char*, 4> kKnownServices = {
    "user",
    "social",
    "message",
    "group",
};

std::string NormalizeRoot(std::string root) {
    while (root.size() > 1 && root.back() == '/') {
        root.pop_back();
    }
    return root;
}

bool EventTouchesService(
    const WatchEvent& event,
    const std::string& service_root,
    const std::string& service_name
) {
    const std::string service_path =
        service_root + "/" + service_name;
    return event.path == service_path ||
           event.path.rfind(service_path + "/", 0) == 0;
}

}  // namespace

struct ZooKeeperServiceDiscovery::ControlState {
    std::mutex mutex;
    std::condition_variable cv;
    bool running{true};
    std::set<std::string> pending;
};

ZooKeeperServiceDiscovery::ZooKeeperServiceDiscovery(
    std::shared_ptr<ZooKeeperClient> client,
    std::string service_root,
    ServiceDiscoveryConfig config
)
    : client_(std::move(client)),
      service_root_(NormalizeRoot(std::move(service_root))),
      config_(std::move(config)) {
    std::unique_lock<std::shared_mutex> lock(snapshot_mutex_);
    for (const char* service : kKnownServices) {
        snapshots_.emplace(
            std::string(service),
            ServiceDiscoverySnapshot{}
        );
    }
}

ZooKeeperServiceDiscovery::~ZooKeeperServiceDiscovery() {
    Stop();
}

bool ZooKeeperServiceDiscovery::Start(
    std::chrono::milliseconds timeout
) {
    if (client_ == nullptr ||
        service_root_.empty() ||
        service_root_.front() != '/' ||
        timeout <= std::chrono::milliseconds::zero()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = "invalid ZooKeeper service discovery configuration";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (running_ || control_thread_.joinable()) {
            last_error_ = "ZooKeeper service discovery already started";
            return false;
        }
        if (!client_->Running() || !client_->Connected()) {
            last_error_ = "ZooKeeper client must be connected before discovery start";
            return false;
        }
        running_ = true;
        ready_ = false;
        last_error_.clear();
    }

    // Start() always establishes a fresh authoritative initial view.  A prior
    // Stop()/Start() cycle must not become "ready" from snapshots left over
    // from the previous run before all known service paths are refreshed.
    {
        std::unique_lock<std::shared_mutex> lock(snapshot_mutex_);
        for (const char* service : kKnownServices) {
            snapshots_[service] = ServiceDiscoverySnapshot{};
        }
    }

    control_state_ = std::make_shared<ControlState>();
    const std::weak_ptr<ControlState> weak_control = control_state_;
    const std::string root = service_root_;

    observer_id_ = client_->AddWatchObserver(
        [weak_control, root](const WatchEvent& event) {
            const auto control = weak_control.lock();
            if (!control) {
                return;
            }

            std::lock_guard<std::mutex> lock(control->mutex);
            if (!control->running) {
                return;
            }

            if (event.type == WatchEventType::kSession) {
                for (const char* service : kKnownServices) {
                    control->pending.insert(service);
                }
            } else {
                for (const char* service : kKnownServices) {
                    if (EventTouchesService(event, root, service)) {
                        control->pending.insert(service);
                    }
                }
            }
            control->cv.notify_one();
        }
    );

    if (observer_id_ == 0) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        running_ = false;
        last_error_ = "failed to install ZooKeeper discovery watch observer";
        control_state_.reset();
        return false;
    }

    observed_session_generation_ = 0;
    control_thread_ = std::thread(
        &ZooKeeperServiceDiscovery::ControlLoop,
        this
    );
    QueueRefreshAll();

    std::unique_lock<std::mutex> lock(state_mutex_);
    const bool initialized = state_cv_.wait_for(
        lock,
        timeout,
        [this]() {
            return !running_ || ready_;
        }
    );

    if (initialized && ready_) {
        return true;
    }

    if (last_error_.empty()) {
        last_error_ = "ZooKeeper discovery initial synchronization timeout";
    }
    lock.unlock();
    Stop();
    return false;
}

void ZooKeeperServiceDiscovery::Stop() {
    const auto observer_id = observer_id_;
    observer_id_ = 0;
    if (client_ && observer_id != 0) {
        client_->RemoveWatchObserver(observer_id);
    }

    const auto control = control_state_;
    if (control) {
        std::lock_guard<std::mutex> lock(control->mutex);
        control->running = false;
        control->cv.notify_all();
    }

    if (control_thread_.joinable()) {
        control_thread_.join();
    }
    control_state_.reset();

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        running_ = false;
        ready_ = false;
        state_cv_.notify_all();
    }
}

bool ZooKeeperServiceDiscovery::Running() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return running_;
}

bool ZooKeeperServiceDiscovery::Ready() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return running_ && ready_;
}

ServiceDiscoverySnapshot ZooKeeperServiceDiscovery::Snapshot(
    const std::string& service_name
) const {
    std::shared_lock<std::shared_mutex> lock(snapshot_mutex_);
    const auto it = snapshots_.find(service_name);
    if (it == snapshots_.end()) {
        return {};
    }
    return it->second;
}

std::string ZooKeeperServiceDiscovery::LastError() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return last_error_;
}

void ZooKeeperServiceDiscovery::ControlLoop() {
    while (true) {
        const auto control = control_state_;
        if (!control) {
            return;
        }

        std::set<std::string> pending;
        {
            std::unique_lock<std::mutex> lock(control->mutex);
            control->cv.wait_for(
                lock,
                kControlTick,
                [&control]() {
                    return !control->running ||
                           !control->pending.empty();
                }
            );
            if (!control->running) {
                return;
            }
            pending.swap(control->pending);
        }

        if (!client_->Connected()) {
            MarkAllRefreshFailed("ZooKeeper control plane is disconnected");
            continue;
        }

        const auto session_generation = client_->SessionGeneration();
        if (session_generation != observed_session_generation_) {
            observed_session_generation_ = session_generation;
            for (const char* service : kKnownServices) {
                pending.insert(service);
            }
        }

        if (pending.empty() &&
            (!AllInitialized() || AnyRefreshFailed())) {
            for (const char* service : kKnownServices) {
                const auto snapshot = Snapshot(service);
                if (!snapshot.initialized || !snapshot.last_refresh_ok) {
                    pending.insert(service);
                }
            }
        }

        for (const auto& service_name : pending) {
            RefreshService(service_name);
        }
    }
}

bool ZooKeeperServiceDiscovery::RefreshService(
    const std::string& service_name
) {
    const std::string service_path = ServicePath(service_name);

    std::vector<std::string> children;
    OperationStatus status = client_->GetChildren(
        service_path,
        true,
        &children
    );

    if (status == OperationStatus::kNoNode) {
        bool exists = false;
        status = client_->Exists(service_path, true, &exists);
        if (status != OperationStatus::kOk) {
            MarkRefreshFailed(
                service_name,
                std::string("install parent existence watch failed: ") +
                    ToString(status)
            );
            return false;
        }

        if (!exists) {
            PublishSnapshot(service_name, {});
            return true;
        }

        // Parent was created between get-children and exists. Read again and
        // arm the child watch rather than publishing a transient empty set.
        status = client_->GetChildren(
            service_path,
            true,
            &children
        );
    }

    if (status != OperationStatus::kOk) {
        MarkRefreshFailed(
            service_name,
            std::string("get children failed: ") + ToString(status)
        );
        return false;
    }

    std::sort(children.begin(), children.end());

    std::vector<ServiceInstance> candidate;
    candidate.reserve(children.size());
    std::unordered_set<std::string> targets;

    for (const auto& child : children) {
        NodeRecord node;
        const auto get_status = client_->GetNode(
            service_path + "/" + child,
            &node
        );

        if (get_status == OperationStatus::kNoNode) {
            // Normal race: child vanished after getChildren(). The armed child
            // watch will trigger another full refresh.
            continue;
        }
        if (get_status != OperationStatus::kOk) {
            // Except for the normal getChildren()->GetNode() disappearance
            // race handled above, a child read failure means this refresh is
            // not authoritative.  Never publish a partial membership view on
            // a control-plane read error; retain the last-known-good snapshot.
            MarkRefreshFailed(
                service_name,
                std::string("read child failed: ") + ToString(get_status)
            );
            return false;
        }

        const auto instance = ServiceInstance::Deserialize(node.data);
        if (!instance.has_value() ||
            instance->service_name != service_name ||
            instance->instance_id != child ||
            instance->protocol != "grpc" ||
            node.ephemeral_owner == 0) {
            LOG_WARN(
                "ZooKeeper discovery skipped invalid/non-ephemeral instance"
                << ", service=" << service_name
                << ", child=" << child
                << ", ephemeral_owner=" << node.ephemeral_owner
            );
            continue;
        }

        if (!targets.insert(instance->target).second) {
            LOG_WARN(
                "ZooKeeper discovery skipped duplicate service target"
                << ", service=" << service_name
                << ", target=" << instance->target
            );
            continue;
        }
        candidate.push_back(*instance);
    }

    std::sort(
        candidate.begin(),
        candidate.end(),
        [](const ServiceInstance& lhs, const ServiceInstance& rhs) {
            if (lhs.instance_id != rhs.instance_id) {
                return lhs.instance_id < rhs.instance_id;
            }
            return lhs.target < rhs.target;
        }
    );

    PublishSnapshot(service_name, std::move(candidate));
    return true;
}

void ZooKeeperServiceDiscovery::PublishSnapshot(
    const std::string& service_name,
    std::vector<ServiceInstance> instances
) {
    std::uint64_t generation = 0;
    {
        std::unique_lock<std::shared_mutex> lock(snapshot_mutex_);
        auto& snapshot = snapshots_[service_name];
        snapshot.instances = std::move(instances);
        ++snapshot.generation;
        snapshot.initialized = true;
        snapshot.last_refresh_ok = true;
        snapshot.refreshed_at = std::chrono::steady_clock::now();
        snapshot.control_plane_unhealthy_since = {};
        snapshot.last_error.clear();
        generation = snapshot.generation;
    }

    const bool all_initialized = AllInitialized();
    const bool any_refresh_failed = AnyRefreshFailed();
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (all_initialized) {
            ready_ = true;
        }
        if (!any_refresh_failed) {
            last_error_.clear();
        }
        state_cv_.notify_all();
    }

    const auto snapshot = Snapshot(service_name);
    LOG_INFO(
        "ZooKeeper discovery snapshot refreshed"
        << ", service=" << service_name
        << ", generation=" << generation
        << ", instance_count=" << snapshot.instances.size()
    );
}

void ZooKeeperServiceDiscovery::MarkRefreshFailed(
    const std::string& service_name,
    const std::string& error
) {
    {
        std::unique_lock<std::shared_mutex> lock(snapshot_mutex_);
        auto& snapshot = snapshots_[service_name];
        if (snapshot.last_refresh_ok ||
            snapshot.control_plane_unhealthy_since ==
                std::chrono::steady_clock::time_point{}) {
            snapshot.control_plane_unhealthy_since =
                std::chrono::steady_clock::now();
        }
        snapshot.last_refresh_ok = false;
        snapshot.last_error = error;
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = service_name + ": " + error;
        state_cv_.notify_all();
    }
}

void ZooKeeperServiceDiscovery::MarkAllRefreshFailed(
    const std::string& error
) {
    for (const char* service : kKnownServices) {
        MarkRefreshFailed(service, error);
    }
}

void ZooKeeperServiceDiscovery::QueueRefresh(
    const std::string& service_name
) {
    const auto control = control_state_;
    if (!control) {
        return;
    }
    std::lock_guard<std::mutex> lock(control->mutex);
    if (!control->running) {
        return;
    }
    control->pending.insert(service_name);
    control->cv.notify_one();
}

void ZooKeeperServiceDiscovery::QueueRefreshAll() {
    for (const char* service : kKnownServices) {
        QueueRefresh(service);
    }
}

bool ZooKeeperServiceDiscovery::AllInitialized() const {
    std::shared_lock<std::shared_mutex> lock(snapshot_mutex_);
    for (const char* service : kKnownServices) {
        const auto it = snapshots_.find(service);
        if (it == snapshots_.end() || !it->second.initialized) {
            return false;
        }
    }
    return true;
}

bool ZooKeeperServiceDiscovery::AnyRefreshFailed() const {
    std::shared_lock<std::shared_mutex> lock(snapshot_mutex_);
    for (const char* service : kKnownServices) {
        const auto it = snapshots_.find(service);
        if (it == snapshots_.end() || !it->second.last_refresh_ok) {
            return true;
        }
    }
    return false;
}

std::string ZooKeeperServiceDiscovery::ServicePath(
    const std::string& service_name
) const {
    return service_root_ + "/" + service_name;
}

}  // namespace tinyimx::registry::zookeeper
