#pragma once

#include "gateway/GatewayServer.h"
#include "services/rpc/MessageRpcClient.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace tinyimx {

struct GroupFanoutCoordinatorOptions {
    std::string gateway_id;
    std::size_t batch_size{64};
    std::uint32_t lease_ms{5000};
    std::uint32_t submitted_retry_ms{3000};
    std::uint32_t failure_retry_ms{1000};
    std::chrono::milliseconds recovery_interval{1000};

    // Test-only deterministic crash window. Disabled by default. When non-zero,
    // the coordinator pauses after a non-empty durable claim has committed but
    // before dispatch/completion. A fault harness can SIGKILL the process in
    // this window and prove that the lease expires and another coordinator
    // reclaims the same durable delivery row.
    std::chrono::milliseconds fault_pause_after_claim{0};
};

// Explicit dependency boundary keeps the coordinator deterministic and unit-testable.
// Production wiring adapts MessageRpcClient + GatewayServer into these callbacks;
// tests can exercise lease/dispatch/completion semantics without opening sockets.
struct GroupFanoutCoordinatorDependencies {
    using ClaimCallback = std::function<
        rpc::RpcResult<rpc::ClaimGroupMessageDeliveriesRpcResponse>(
            const rpc::ClaimGroupMessageDeliveriesRpcRequest&,
            const rpc::RpcCallOptions&)>;

    using CompleteCallback = std::function<
        rpc::MessageMutationRpcCallResult(
            const rpc::CompleteGroupMessageDeliveryAttemptRpcRequest&,
            const rpc::RpcCallOptions&)>;

    using DispatchCallback = std::function<
        GroupFanoutDispatchResult(const rpc::GroupDeliveryWorkRpcRecord&)>;

    ClaimCallback claim;
    CompleteCallback complete;
    DispatchCallback dispatch;

    [[nodiscard]] bool Valid() const noexcept {
        return static_cast<bool>(claim) &&
               static_cast<bool>(complete) &&
               static_cast<bool>(dispatch);
    }
};

class GroupFanoutCoordinator {
public:
    GroupFanoutCoordinator(
        rpc::MessageRpcClient* message_rpc_client,
        GatewayServer* gateway,
        GroupFanoutCoordinatorOptions options
    );

    // Deterministic constructor used by focused tests and fault simulation.
    GroupFanoutCoordinator(
        GroupFanoutCoordinatorDependencies dependencies,
        GroupFanoutCoordinatorOptions options
    );

    ~GroupFanoutCoordinator();

    GroupFanoutCoordinator(const GroupFanoutCoordinator&) = delete;
    GroupFanoutCoordinator& operator=(const GroupFanoutCoordinator&) = delete;

    bool Start();
    void Stop();
    [[nodiscard]] bool IsRunning() const noexcept;

    // Execute exactly one durable recovery iteration without starting the worker.
    std::size_t RunOneIterationForTest();

private:
    void Run(std::stop_token token);
    std::size_t RunOneIteration();
    std::string NextLeaseToken();
    rpc::RpcCallOptions MakeCallOptions(const std::string& operation) const;

private:
    GroupFanoutCoordinatorDependencies dependencies_;
    GroupFanoutCoordinatorOptions options_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> lease_sequence_{1};
    std::jthread worker_;
};

}  // namespace tinyimx
