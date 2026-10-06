#include "gateway/GroupFanoutCoordinator.h"
#include "gateway/GroupFanoutWakeup.h"
#include "gateway/GroupFanoutPipeline.h"

#include "common/logging/LogMacros.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

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

    if (message_rpc_client != nullptr) {
        dependencies.complete_batch = [message_rpc_client](
            const std::vector<rpc::CompleteGroupMessageDeliveryAttemptRpcRequest>& attempts,
            const rpc::RpcCallOptions& options
        ) {
            return message_rpc_client->CompleteGroupMessageDeliveryAttempts(attempts, options);
        };
    }

    if (gateway != nullptr) {
        dependencies.dispatch_batch = [gateway](
            const std::vector<rpc::GroupDeliveryWorkRpcRecord>& work) {
            return gateway->DispatchGroupFanoutDeliveries(work);
        };
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
        if (GroupFanoutCommitWakeEnabled()) LocalGroupFanoutWakeState().cv.notify_all();
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
    if (GroupFanoutCommitWakeEnabled()) {
        auto& wake = LocalGroupFanoutWakeState();
        std::uint64_t observed = 0;
        {
            std::lock_guard<std::mutex> lock(wake.mutex);
            observed = wake.generation;
        }
        unsigned full_batches = 0;
        while (!token.stop_requested() && running_.load(std::memory_order_acquire)) {
            const auto processed = RunOneIteration();
            const bool full = processed >= std::min<std::size_t>(options_.batch_size, 256);
            // SKIP LOCKED can return a nonempty partial batch while other
            // eligible rows are locked. Recheck within the same bounded burst.
            const bool drain = full ||
                (GroupFanoutPartialDrainEnabled() && processed != 0);
            // Drain a bounded number of full batches before yielding. Work still
            // uses the original durable claim, lease token and completion fence.
            if (drain && ++full_batches < 4) continue;
            std::unique_lock<std::mutex> lock(wake.mutex);
            if (drain) {
                wake.cv.wait_for(lock, std::chrono::milliseconds(25), [&] {
                    return token.stop_requested() ||
                           !running_.load(std::memory_order_acquire);
                });
            } else {
                wake.cv.wait_for(lock, options_.recovery_interval, [&] {
                    return token.stop_requested() ||
                           !running_.load(std::memory_order_acquire) ||
                           wake.generation != observed;
                });
            }
            observed = wake.generation;
            full_batches = 0;
        }
        return;
    }
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

    diagnostics::GroupFanoutPhaseTrace trace(GroupFanoutDeferCompletionEnabled());
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

    const auto& items = claimed.value->work_items;
    trace.Claimed(items.size(), items.empty() ? 0 : items.front().message.message_id,
                  items.empty() ? 0 : items.back().message.message_id);
    // No buffer growth for unexpected responses larger than the <=256 claim.
    const bool defer = GroupFanoutDeferCompletionEnabled() && items.size() <= claim.limit;
    std::vector<rpc::CompleteGroupMessageDeliveryAttemptRpcRequest> pending;
    if (defer) pending.reserve(items.size());
    const auto finish = [&](const rpc::CompleteGroupMessageDeliveryAttemptRpcRequest& complete) {
        const auto begin = trace.Mark();
        const auto completed = dependencies_.complete(
            complete, MakeCallOptions("complete-group-delivery-attempt"));
        trace.CompleteDone(begin);
        if (!completed.ok()) {
            LOG_WARN("group fanout attempt completion uncertain"
                     << ", message_id=" << complete.message_id
                     << ", recipient=" << complete.recipient_user_id
                     << ", attempted=" << completed.attempted
                     << ", error=" << completed.status.message);
        }
    };

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

    std::vector<rpc::GroupDeliveryWorkRpcRecord> dispatch_items;
    std::vector<GroupFanoutDispatchResult> batch_dispatch;
    const bool use_route_batch = defer && GroupFanoutRouteBatchEnabled() &&
        static_cast<bool>(dependencies_.dispatch_batch);
    if (use_route_batch) {
        dispatch_items.reserve(items.size());
        for (const auto& work : items) {
            if (work.message.message_id != 0 &&
                work.delivery.message_id == work.message.message_id &&
                work.delivery.recipient_user_id != 0)
                dispatch_items.push_back(work);
        }
        if (!dispatch_items.empty()) {
            const auto begin = trace.Mark();
            batch_dispatch = dependencies_.dispatch_batch(dispatch_items);
            trace.DispatchBatchDone(begin, dispatch_items.size());
            if (batch_dispatch.size() != dispatch_items.size()) {
                // Submission can already have happened. Preserve the original
                // retry/lease path instead of dispatching any recipient twice.
                batch_dispatch.assign(dispatch_items.size(), GroupFanoutDispatchResult{});
                for (auto& result : batch_dispatch)
                    result.error_code = "batch_dispatch_result_mismatch";
                LOG_WARN("group fanout batch dispatch result mismatch");
            }
        }
    }
    std::size_t dispatch_index = 0;
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

        GroupFanoutDispatchResult dispatched;
        if (use_route_batch) {
            dispatched = batch_dispatch[dispatch_index++];
        } else {
            const auto dispatch_start = trace.Mark();
            dispatched = dependencies_.dispatch(work);
            trace.DispatchDone(dispatch_start);
        }

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

        if (defer) pending.push_back(std::move(complete));
        else finish(complete);
        ++processed;
    }
    // Original lease/status fence prevents completion from reverting an ACK's
    // DELIVERED state. Every dispatch follows the original committed claim.
    if (!pending.empty() && GroupFanoutCompletionBatchEnabled() && dependencies_.complete_batch) {
        const auto begin = trace.Mark();
        const auto completed = dependencies_.complete_batch(
            pending, MakeCallOptions("complete-group-delivery-attempts"));
        trace.CompleteBatchDone(begin, pending.size());
        if (!completed.ok()) {
            // A transport error may follow a committed statement. Do not replay
            // single completions here; the unchanged lease/recovery path owns it.
            LOG_WARN("group fanout batch completion uncertain"
                     << ", count=" << pending.size()
                     << ", attempted=" << completed.attempted
                     << ", error=" << completed.status.message);
        }
    } else {
        for (const auto& complete : pending) finish(complete);
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
