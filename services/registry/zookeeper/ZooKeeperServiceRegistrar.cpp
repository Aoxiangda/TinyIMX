#include "services/registry/zookeeper/ZooKeeperServiceRegistrar.h"

#include "common/logging/LogMacros.h"
#include "services/registry/zookeeper/ZooKeeperClient.h"
#include "services/registry/zookeeper/ZooKeeperTypes.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace tinyimx::registry::zookeeper {
namespace {

constexpr auto kReconcileInterval =
    std::chrono::milliseconds(200);

std::string ServiceParent(
    const std::string& root,
    const std::string& service
) {
    std::string normalized = root;
    while (normalized.size() > 1 && normalized.back() == '/') {
        normalized.pop_back();
    }
    return normalized + "/" + service;
}

}  // namespace

ZooKeeperServiceRegistrar::ZooKeeperServiceRegistrar(
    ZooKeeperClient* client,
    ServiceInstance instance,
    std::string service_root
)
    : client_(client),
      instance_(std::move(instance)),
      service_root_(std::move(service_root)),
      registration_path_(
          instance_.BuildPath(service_root_)
      ),
      serialized_instance_(instance_.Serialize()) {
}

ZooKeeperServiceRegistrar::~ZooKeeperServiceRegistrar() {
    Stop();
}

bool ZooKeeperServiceRegistrar::Start(
    std::chrono::milliseconds timeout
) {
    if (client_ == nullptr ||
        !instance_.Valid() ||
        service_root_.empty() ||
        service_root_.front() != '/' ||
        timeout.count() <= 0) {
        SetError("invalid service registrar configuration");
        return false;
    }

    if (control_thread_.joinable()) {
        SetError("service registrar already started");
        return false;
    }

    stopping_.store(false);
    if (!RegisterWithin(timeout)) {
        return false;
    }

    control_thread_ = std::thread(
        &ZooKeeperServiceRegistrar::ControlLoop,
        this
    );
    return true;
}

void ZooKeeperServiceRegistrar::Stop() {
    stopping_.store(true);

    if (control_thread_.joinable()) {
        control_thread_.join();
    }

    UnregisterOwnedNode();

    std::lock_guard<std::mutex> lock(state_mutex_);
    registered_ = false;
    registered_generation_ = 0;
}

bool ZooKeeperServiceRegistrar::Registered() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return registered_;
}

std::string ZooKeeperServiceRegistrar::RegistrationPath() const {
    return registration_path_;
}

std::string ZooKeeperServiceRegistrar::LastError() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return last_error_;
}

std::uint64_t ZooKeeperServiceRegistrar::RegisteredGeneration() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return registered_generation_;
}

void ZooKeeperServiceRegistrar::ControlLoop() {
    while (!stopping_.load()) {
        const auto generation = client_->SessionGeneration();
        const bool connected = client_->Connected();

        bool needs_reconcile = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (registered_ &&
                registered_generation_ != generation) {
                registered_ = false;
            }
            needs_reconcile =
                connected &&
                (!registered_ ||
                 registered_generation_ != generation);
        }

        if (needs_reconcile) {
            if (!RegisterWithin(
                    std::chrono::milliseconds(2000)
                )) {
                LOG_WARN(
                    "ZooKeeper service registration recovery pending"
                    << ", path=" << registration_path_
                    << ", generation=" << generation
                    << ", error=" << LastError()
                );
            } else {
                LOG_INFO(
                    "ZooKeeper service registration recovered"
                    << ", path=" << registration_path_
                    << ", generation="
                    << RegisteredGeneration()
                );
            }
        }

        std::this_thread::sleep_for(kReconcileInterval);
    }
}

bool ZooKeeperServiceRegistrar::RegisterWithin(
    std::chrono::milliseconds timeout
) {
    const auto deadline =
        std::chrono::steady_clock::now() + timeout;

    while (!stopping_.load() &&
           std::chrono::steady_clock::now() < deadline) {
        if (!client_->Connected()) {
            std::this_thread::sleep_for(kReconcileInterval);
            continue;
        }

        if (!EnsureParents()) {
            std::this_thread::sleep_for(kReconcileInterval);
            continue;
        }

        const auto result = ReconcileOnce();
        if (result == ReconcileResult::kRegistered) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            registered_ = true;
            registered_generation_ =
                client_->SessionGeneration();
            last_error_.clear();
            return true;
        }

        if (result == ReconcileResult::kOwnershipConflict ||
            result == ReconcileResult::kFatal) {
            return false;
        }

        std::this_thread::sleep_for(kReconcileInterval);
    }

    SetError("service registration confirmation timeout");
    return false;
}

ZooKeeperServiceRegistrar::ReconcileResult
ZooKeeperServiceRegistrar::ReconcileOnce() {
    const auto create_status = client_->CreateEphemeral(
        registration_path_,
        serialized_instance_
    );

    if (create_status != OperationStatus::kOk &&
        create_status != OperationStatus::kNodeExists &&
        create_status != OperationStatus::kConnectionLoss &&
        create_status != OperationStatus::kSessionExpired &&
        create_status != OperationStatus::kNotConnected) {
        SetError(
            std::string("create ephemeral failed: ") +
            ToString(create_status)
        );
        return ReconcileResult::kFatal;
    }

    // A create response may be lost after the server commits it. Always read
    // reality back before deciding whether another mutation is necessary.
    NodeRecord node;
    const auto get_status = client_->GetNode(
        registration_path_,
        &node
    );

    if (get_status == OperationStatus::kNoNode ||
        get_status == OperationStatus::kConnectionLoss ||
        get_status == OperationStatus::kSessionExpired ||
        get_status == OperationStatus::kNotConnected) {
        return ReconcileResult::kRetryable;
    }
    if (get_status != OperationStatus::kOk) {
        SetError(
            std::string("read registration failed: ") +
            ToString(get_status)
        );
        return ReconcileResult::kFatal;
    }

    const std::int64_t session_id = client_->SessionId();
    if (node.ephemeral_owner != session_id) {
        SetError(
            "registration path is owned by another ZooKeeper session"
        );
        return ReconcileResult::kOwnershipConflict;
    }

    if (node.data != serialized_instance_) {
        SetError(
            "registration path data differs for the current session"
        );
        return ReconcileResult::kFatal;
    }

    return ReconcileResult::kRegistered;
}

bool ZooKeeperServiceRegistrar::EnsureParents() {
    const auto status = client_->EnsurePersistentPath(
        ServiceParent(
            service_root_,
            instance_.service_name
        )
    );

    if (status == OperationStatus::kOk) {
        return true;
    }
    if (status == OperationStatus::kConnectionLoss ||
        status == OperationStatus::kSessionExpired ||
        status == OperationStatus::kNotConnected) {
        return false;
    }

    SetError(
        std::string("ensure registry parent failed: ") +
        ToString(status)
    );
    return false;
}

bool ZooKeeperServiceRegistrar::UnregisterOwnedNode() {
    if (client_ == nullptr || !client_->Connected()) {
        return false;
    }

    NodeRecord node;
    const auto get_status = client_->GetNode(
        registration_path_,
        &node
    );
    if (get_status == OperationStatus::kNoNode) {
        return true;
    }
    if (get_status != OperationStatus::kOk) {
        return false;
    }

    if (node.ephemeral_owner != client_->SessionId()) {
        // Fencing: a delayed shutdown from an old process/session must never
        // delete the registration now owned by a replacement instance.
        return false;
    }

    const auto delete_status = client_->DeleteNode(
        registration_path_,
        node.version
    );
    return delete_status == OperationStatus::kOk ||
           delete_status == OperationStatus::kNoNode;
}

void ZooKeeperServiceRegistrar::SetError(
    const std::string& error
) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    last_error_ = error;
}

}  // namespace tinyimx::registry::zookeeper
