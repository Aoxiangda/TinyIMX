#include "tests/concurrency/TestFramework.h"

#include "gateway/GroupFanoutCoordinator.h"

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

namespace tinyimx::test {

void RegisterGroupFanoutCoordinatorTests(TestRunner& runner) {
    runner.Add(
        "GroupFanoutCoordinator.ClaimsDispatchesAndCompletesDeterministically",
        []() {
            std::vector<rpc::CompleteGroupMessageDeliveryAttemptRpcRequest> completions;
            std::size_t claim_count = 0;
            std::size_t dispatch_count = 0;

            GroupFanoutCoordinatorDependencies deps;
            deps.claim = [&](const rpc::ClaimGroupMessageDeliveriesRpcRequest& request,
                             const rpc::RpcCallOptions&) {
                ++claim_count;
                TINYIMX_EXPECT_EQ(request.lease_owner, std::string("gateway-a"));
                TINYIMX_EXPECT_TRUE(!request.lease_token.empty());
                TINYIMX_EXPECT_EQ(request.limit, static_cast<std::uint32_t>(8));

                rpc::ClaimGroupMessageDeliveriesRpcResponse response;
                for (std::uint64_t recipient : {10002ULL, 10003ULL}) {
                    rpc::GroupDeliveryWorkRpcRecord work;
                    work.message.message_id = 7001;
                    work.message.group_id = 9001;
                    work.message.from_user_id = 10001;
                    work.message.message_type = 1;
                    work.message.content = "hello";
                    work.delivery.message_id = 7001;
                    work.delivery.group_id = 9001;
                    work.delivery.recipient_user_id = recipient;
                    work.delivery.delivery_state = rpc::GroupDeliveryRpcState::kPending;
                    response.work_items.push_back(std::move(work));
                }
                return rpc::RpcResult<rpc::ClaimGroupMessageDeliveriesRpcResponse>::Success(
                    std::move(response));
            };

            deps.dispatch = [&](const rpc::GroupDeliveryWorkRpcRecord& work) {
                ++dispatch_count;
                GroupFanoutDispatchResult result;
                result.gateway_id = work.delivery.recipient_user_id == 10002
                    ? "gateway-a" : "gateway-b";
                result.status = work.delivery.recipient_user_id == 10002
                    ? GroupFanoutDispatchStatus::kSubmitted
                    : GroupFanoutDispatchStatus::kOffline;
                return result;
            };

            deps.complete = [&](const rpc::CompleteGroupMessageDeliveryAttemptRpcRequest& request,
                                const rpc::RpcCallOptions&) {
                completions.push_back(request);
                rpc::MessageMutationRpcResponse response;
                response.affected_rows = 1;
                return rpc::MessageMutationRpcCallResult::Success(response);
            };

            GroupFanoutCoordinatorOptions options;
            options.gateway_id = "gateway-a";
            options.batch_size = 8;
            options.lease_ms = 5000;
            options.submitted_retry_ms = 3000;
            options.failure_retry_ms = 1000;
            options.recovery_interval = std::chrono::milliseconds(1000);

            GroupFanoutCoordinator coordinator(std::move(deps), options);
            TINYIMX_EXPECT_EQ(coordinator.RunOneIterationForTest(), static_cast<std::size_t>(2));
            TINYIMX_EXPECT_EQ(claim_count, static_cast<std::size_t>(1));
            TINYIMX_EXPECT_EQ(dispatch_count, static_cast<std::size_t>(2));
            TINYIMX_EXPECT_EQ(completions.size(), static_cast<std::size_t>(2));
            TINYIMX_EXPECT_EQ(completions[0].message_id, static_cast<std::uint64_t>(7001));
            TINYIMX_EXPECT_EQ(completions[0].recipient_user_id, static_cast<std::uint64_t>(10002));
            TINYIMX_EXPECT_EQ(completions[0].outcome, rpc::GroupDeliveryAttemptRpcOutcome::kSubmitted);
            TINYIMX_EXPECT_EQ(completions[1].recipient_user_id, static_cast<std::uint64_t>(10003));
            TINYIMX_EXPECT_EQ(completions[1].outcome, rpc::GroupDeliveryAttemptRpcOutcome::kOffline);
        }
    );

    runner.Add(
        "GroupFanoutCoordinator.InvalidDependenciesFailClosed",
        []() {
            GroupFanoutCoordinatorDependencies deps;
            GroupFanoutCoordinatorOptions options;
            options.gateway_id = "gateway-a";
            GroupFanoutCoordinator coordinator(std::move(deps), options);
            TINYIMX_EXPECT_TRUE(!coordinator.Start());
            TINYIMX_EXPECT_EQ(coordinator.RunOneIterationForTest(), static_cast<std::size_t>(0));
        }
    );
}

}  // namespace tinyimx::test
