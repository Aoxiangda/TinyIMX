#include "gateway/GroupFanoutCoordinator.h"

#include "common/logging/LogMacros.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

namespace tinyimx {
namespace {

GroupFanoutCoordinatorDependencies MakeProductionDependencies(
    rpc::MessageRpcClient* message_rpc_client,
    GatewayServer* gateway
) {
    GroupFanoutCoordinatorDependencies dependencies;

    if (message_rpc_client != nullptr) {
        dependencies.claim = [message_rpc_client](
            const rpc::ClaimGroupMessageDeliveriesRpcRequest& request,
            const rpc::RpcCallOptions& options
        ) {
            return message_rpc_client->ClaimGroupMessageDeliveries(request, options);
        };

        dependencies.complete = [message_rpc_client](
            const rpc::CompleteGroupMessageDeliveryAttemptRpcRequest& request,
            const rpc::RpcCallOptions& options
        ) {
            return message_rpc_client->CompleteGroupMessageDeliveryAttempt(request, options);
        };
    }

    if (gateway != nullptr) {
        dependencies.dispatch = [gateway](const rpc::GroupDeliveryWorkRpcRecord& work) {
            return gateway->DispatchGroupFanoutDelivery(work);
        };
    }

    return dependencies;
}

}  // namespace

GroupFanoutCoordinator::GroupFanoutCoordinator(
    rpc::MessageRpcClient* message_rpc_client,
    GatewayServer* gateway,
    GroupFanoutCoordinatorOptions options
) : GroupFanoutCoordinator(
        MakeProductionDependencies(message_rpc_client, gateway),
        std::move(options)) {}

GroupFanoutCoordinator::GroupFanoutCoordinator(
    GroupFanoutCoordinatorDependencies dependencies,
    GroupFanoutCoordinatorOptions options
) : dependencies_(std::move(dependencies)),
    options_(std::move(options)) {}

GroupFanoutCoordinator::~GroupFanoutCoordinator() { Stop(); }

bool GroupFanoutCoordinator::Start() {
    if (!dependencies_.Valid() || options_.gateway_id.empty() ||
        options_.batch_size == 0 || options_.batch_size > 256 ||
        options_.lease_ms < 100 ||
        options_.recovery_interval <= std::chrono::milliseconds::zero()) {
        return false;
    }

    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return true;
    }

    worker_ = std::jthread([this](std::stop_token token) { Run(token); });
    LOG_INFO("group fanout coordinator started"
             << ", gateway_id=" << options_.gateway_id
             << ", batch_size=" << options_.batch_size
             << ", lease_ms=" << options_.lease_ms
             << ", recovery_interval_ms=" << options_.recovery_interval.count());
    return true;
}

void GroupFanoutCoordinator::Stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    LOG_INFO("group fanout coordinator stopped" << ", gateway_id=" << options_.gateway_id);
}

bool GroupFanoutCoordinator::IsRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

std::size_t GroupFanoutCoordinator::RunOneIterationForTest() {
    return RunOneIteration();
}

void GroupFanoutCoordinator::Run(std::stop_token token) {
    while (!token.stop_requested() && running_.load(std::memory_order_acquire)) {
        RunOneIteration();
        const auto until = std::chrono::steady_clock::now() + options_.recovery_interval;
        while (!token.stop_requested() && std::chrono::steady_clock::now() < until) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }
}

std::size_t GroupFanoutCoordinator::RunOneIteration() {
    if (!dependencies_.Valid()) {
        return 0;
    }

    const std::string lease_token = NextLeaseToken();

    rpc::ClaimGroupMessageDeliveriesRpcRequest claim;
    claim.lease_owner = options_.gateway_id;
    claim.lease_token = lease_token;
    claim.limit = static_cast<std::uint32_t>(
        std::min<std::size_t>(options_.batch_size, 256));
    claim.lease_ms = options_.lease_ms;
    claim.message_id = 0;

    const auto claimed = dependencies_.claim(
        claim, MakeCallOptions("claim-group-deliveries"));
    if (!claimed.ok()) {
        LOG_WARN("group fanout claim failed"
                 << ", gateway_id=" << options_.gateway_id
                 << ", error=" << claimed.status.message);
        return 0;
    }

    if (!claimed.value->work_items.empty() &&
        options_.fault_pause_after_claim.count() > 0) {
        const auto& first = claimed.value->work_items.front();
        LOG_WARN("group fanout fault pause after durable claim"
                 << ", gateway_id=" << options_.gateway_id
                 << ", message_id=" << first.message.message_id
                 << ", recipient=" << first.delivery.recipient_user_id
                 << ", lease_ms=" << options_.lease_ms
                 << ", pause_ms=" << options_.fault_pause_after_claim.count());
        std::this_thread::sleep_for(options_.fault_pause_after_claim);
    }

    std::size_t processed = 0;
    for (const auto& work : claimed.value->work_items) {
        if (work.message.message_id == 0 ||
            work.delivery.message_id != work.message.message_id ||
            work.delivery.recipient_user_id == 0) {
            LOG_ERROR("group fanout rejected invalid claimed work item"
                      << ", message_id=" << work.message.message_id
                      << ", delivery_message_id=" << work.delivery.message_id
                      << ", recipient=" << work.delivery.recipient_user_id);
            ++processed;
            continue;
        }

        const GroupFanoutDispatchResult dispatched = dependencies_.dispatch(work);

        // Receiver ACK can race and move the row to DELIVERED before this RPC.
        // Completion is guarded by lease/status in MessageService and therefore
        // becomes an affected_rows=0 repair/no-op when confirmation won the race.
        rpc::CompleteGroupMessageDeliveryAttemptRpcRequest complete;
        complete.message_id = work.message.message_id;
        complete.recipient_user_id = work.delivery.recipient_user_id;
        complete.lease_token = lease_token;
        complete.gateway_id = dispatched.gateway_id;

        switch (dispatched.status) {
            case GroupFanoutDispatchStatus::kSubmitted:
            case GroupFanoutDispatchStatus::kAlreadyDelivered:
                complete.outcome = rpc::GroupDeliveryAttemptRpcOutcome::kSubmitted;
                complete.retry_after_ms = options_.submitted_retry_ms;
                break;
            case GroupFanoutDispatchStatus::kOffline:
                complete.outcome = rpc::GroupDeliveryAttemptRpcOutcome::kOffline;
                complete.retry_after_ms = 0;
                complete.error_code = dispatched.error_code.empty()
                    ? "recipient_offline" : dispatched.error_code;
                break;
            case GroupFanoutDispatchStatus::kRetryableFailure:
                complete.outcome = rpc::GroupDeliveryAttemptRpcOutcome::kRetryableFailure;
                complete.retry_after_ms = options_.failure_retry_ms;
                complete.error_code = dispatched.error_code.empty()
                    ? "delivery_retryable_failure" : dispatched.error_code;
                break;
        }

        const auto completed = dependencies_.complete(
            complete, MakeCallOptions("complete-group-delivery-attempt"));
        if (!completed.ok()) {
            LOG_WARN("group fanout attempt completion uncertain"
                     << ", message_id=" << complete.message_id
                     << ", recipient=" << complete.recipient_user_id
                     << ", attempted=" << completed.attempted
                     << ", error=" << completed.status.message);
        }
        ++processed;
    }

    return processed;
}

std::string GroupFanoutCoordinator::NextLeaseToken() {
    const auto sequence = lease_sequence_.fetch_add(1, std::memory_order_relaxed);
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    return options_.gateway_id + ":group-fanout:" +
           std::to_string(micros) + ":" + std::to_string(sequence);
}

rpc::RpcCallOptions GroupFanoutCoordinator::MakeCallOptions(
    const std::string& operation
) const {
    rpc::RpcCallOptions options;
    const auto sequence = lease_sequence_.load(std::memory_order_relaxed);
    options.request_id = options_.gateway_id + ":" + operation + ":" +
                         std::to_string(sequence);
    options.trace_id = options_.gateway_id + ":group-fanout:" +
                       std::to_string(sequence);
    options.caller_service = "gateway";
    options.caller_instance = options_.gateway_id;
    options.remaining_timeout = std::chrono::milliseconds(
        std::max<std::uint32_t>(options_.lease_ms / 2U, 500U));
    return options;
}

}  // namespace tinyimx
