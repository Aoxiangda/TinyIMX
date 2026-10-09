#pragma once

#include "services/registry/zookeeper/ServiceInstance.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace tinyimx::registry::zookeeper {

class ZooKeeperClient;

class ZooKeeperServiceRegistrar final {
public:
    ZooKeeperServiceRegistrar(
        ZooKeeperClient* client,
        ServiceInstance instance,
        std::string service_root
    );
    ~ZooKeeperServiceRegistrar();

    ZooKeeperServiceRegistrar(
        const ZooKeeperServiceRegistrar&
    ) = delete;
    ZooKeeperServiceRegistrar& operator=(
        const ZooKeeperServiceRegistrar&
    ) = delete;

    bool Start(std::chrono::milliseconds timeout);
    void Stop();

    [[nodiscard]] bool Registered() const;
    [[nodiscard]] std::string RegistrationPath() const;
    [[nodiscard]] std::string LastError() const;
    [[nodiscard]] std::uint64_t RegisteredGeneration() const;

private:
    enum class ReconcileResult {
        kRegistered,
        kRetryable,
        kOwnershipConflict,
        kFatal,
    };

    void ControlLoop();
    bool RegisterWithin(std::chrono::milliseconds timeout);
    ReconcileResult ReconcileOnce();
    bool EnsureParents();
    bool UnregisterOwnedNode();
    void SetError(const std::string& error);

private:
    ZooKeeperClient* client_{nullptr};  // non-owning
    ServiceInstance instance_;
    std::string service_root_;
    std::string registration_path_;
    std::string serialized_instance_;

    std::atomic<bool> stopping_{false};
    std::thread control_thread_;

    mutable std::mutex state_mutex_;
    bool registered_{false};
    std::uint64_t registered_generation_{0};
    std::string last_error_;
};

}  // namespace tinyimx::registry::zookeeper
