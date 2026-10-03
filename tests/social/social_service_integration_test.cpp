#include "services/rpc/RpcCallOptions.h"
#include "services/rpc/SocialRpcClient.h"
#include "services/rpc/StaticServiceEndpointProvider.h"
#include "services/social/application/FriendApplicationService.h"
#include "services/social/application/FriendRequestApplicationService.h"
#include "services/social/application/FriendRequestRepositoryPort.h"
#include "services/social/server/SocialServiceServer.h"
#include "services/social/service/SocialServiceImpl.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace {

class FakeFriendRepositoryPort final
    : public tinyimx::social::FriendRepositoryPort {
public:
    tinyimx::social::ChatPermissionApplicationResult
    CheckPrivateChatPermission(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id
    ) override {
        tinyimx::social::ChatPermissionApplicationResult result;
        result.status = tinyimx::social::ChatPermissionApplicationStatus::kAllowed;
        result.message = "allowed";
        last_permission_from_user_id = from_user_id;
        last_permission_to_user_id = to_user_id;
        ++permission_call_count;
        return result;
    }

    tinyimx::social::FriendRepositoryListResult ListFriends(
        std::uint64_t user_id,
        std::size_t limit
    ) override {
        ++call_count;
        last_user_id = user_id;
        last_limit = limit;

        tinyimx::social::FriendRepositoryListResult result;
        result.status =
            tinyimx::social::FriendApplicationStatus::kSucceeded;

        for (std::uint64_t id = 10002; id <= 10004; ++id) {
            tinyimx::social::FriendView view;
            view.friend_user_id = id;
            view.username = "user" + std::to_string(id);
            view.nickname = "friend" + std::to_string(id);
            view.user_status = 1;
            view.relation_status = 1;
            view.relation_created_at = "2026-09-03 10:00:00";
            view.relation_updated_at = "2026-09-03 10:00:00";
            result.records.push_back(std::move(view));
        }

        return result;
    }

    std::size_t permission_call_count{0};
    std::uint64_t last_permission_from_user_id{0};
    std::uint64_t last_permission_to_user_id{0};
    std::size_t call_count{0};
    std::uint64_t last_user_id{0};
    std::size_t last_limit{0};
};


class FakeFriendRequestRepositoryPort final
    : public tinyimx::social::FriendRequestRepositoryPort {
public:
    tinyimx::social::CreateFriendRequestApplicationResult Create(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        const std::string& request_message
    ) override {
        ++create_calls;
        tinyimx::social::CreateFriendRequestApplicationResult result;
        result.outcome = create_calls == 1
            ? tinyimx::social::FriendRequestCreateOutcome::kCreated
            : tinyimx::social::FriendRequestCreateOutcome::kAlreadyPending;
        result.request_id = 7001;
        result.message = create_calls == 1 ? "created" : "already pending";
        last_from_user_id = from_user_id;
        last_to_user_id = to_user_id;
        last_message = request_message;
        return result;
    }

    tinyimx::social::ListPendingFriendRequestsApplicationResult
    ListPendingIncoming(
        std::uint64_t receiver_user_id,
        const std::string&,
        std::uint64_t,
        std::size_t
    ) override {
        ++list_calls;
        tinyimx::social::ListPendingFriendRequestsApplicationResult result;
        result.outcome = tinyimx::social::FriendRequestListOutcome::kSucceeded;
        tinyimx::social::FriendRequestView view;
        view.request_id = 7001;
        view.from_user_id = 10001;
        view.to_user_id = receiver_user_id;
        view.request_status = 0;
        view.created_at = "2026-09-29 12:00:00";
        result.requests.push_back(std::move(view));
        result.message = "ok";
        return result;
    }

    tinyimx::social::AcceptFriendRequestApplicationResult Accept(
        std::uint64_t request_id,
        std::uint64_t handler_user_id
    ) override {
        ++accept_calls;
        return {
            tinyimx::social::FriendRequestAcceptOutcome::kAlreadyAccepted,
            request_id,
            10001,
            handler_user_id,
            "already accepted"
        };
    }

    tinyimx::social::RejectFriendRequestApplicationResult Reject(
        std::uint64_t request_id,
        std::uint64_t handler_user_id
    ) override {
        ++reject_calls;
        return {
            tinyimx::social::FriendRequestRejectOutcome::kAlreadyRejected,
            request_id,
            10001,
            handler_user_id,
            "already rejected"
        };
    }

    std::size_t create_calls{0};
    std::size_t list_calls{0};
    std::size_t accept_calls{0};
    std::size_t reject_calls{0};
    std::uint64_t last_from_user_id{0};
    std::uint64_t last_to_user_id{0};
    std::string last_message;
};

bool Expect(bool condition, const char* name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return true;
    }

    std::cerr << "[FAIL] " << name << '\n';
    return false;
}

bool TestRealGrpcVerticalSlice() {
    FakeFriendRepositoryPort repository;
    FakeFriendRequestRepositoryPort friend_request_repository;
    tinyimx::social::FriendApplicationService application(&repository);
    tinyimx::social::FriendRequestApplicationService friend_request_application(
        &friend_request_repository
    );
    tinyimx::social::SocialServiceImpl service_impl(
        &application,
        &friend_request_application
    );
    tinyimx::social::SocialServiceServer server(&service_impl);

    if (!server.Start("127.0.0.1:0")) {
        return Expect(false, "SocialService.RealGrpcServerStart");
    }

    const auto provider =
        std::make_shared<
            tinyimx::rpc::StaticServiceEndpointProvider
        >(server.BoundTarget());

    tinyimx::rpc::SocialRpcClient client(provider);

    tinyimx::rpc::ListFriendsRpcRequest request;
    request.actor_user_id = 10001;
    request.limit = 2;

    tinyimx::rpc::RpcCallOptions options;
    options.request_id = "m14-a3-request-1";
    options.trace_id = "m14-a3-trace-1";
    options.caller_service = "gateway-test";
    options.caller_instance = "gateway-test-a";
    options.remaining_timeout = std::chrono::seconds(1);

    const auto result = client.ListFriends(request, options);

    const bool ok =
        Expect(
            result.ok(),
            "SocialService.RealGrpcListFriendsSuccess"
        ) &&
        Expect(
            result.value->friends.size() == 2 &&
            result.value->has_more,
            "SocialService.RealGrpcResponseMappingAndHasMore"
        ) &&
        Expect(
            repository.call_count == 1 &&
            repository.last_user_id == 10001 &&
            repository.last_limit == 3,
            "SocialService.ApplicationRepositoryBoundary"
        );


    tinyimx::rpc::CheckPrivateChatPermissionRpcRequest permission_request;
    permission_request.from_user_id = 10001;
    permission_request.to_user_id = 10002;
    const auto permission_result =
        client.CheckPrivateChatPermission(permission_request, options);

    tinyimx::rpc::CreateFriendRequestRpcRequest create_request;
    create_request.from_user_id = 10001;
    create_request.to_user_id = 10002;
    create_request.request_message = "hello";
    const auto create_result = client.CreateFriendRequest(create_request, options);

    tinyimx::rpc::ListPendingIncomingFriendRequestsRpcRequest list_request;
    list_request.receiver_user_id = 10002;
    list_request.limit = 20;
    const auto list_result =
        client.ListPendingIncomingFriendRequests(list_request, options);

    tinyimx::rpc::AcceptFriendRequestRpcRequest accept_request;
    accept_request.request_id = 7001;
    accept_request.handler_user_id = 10002;
    const auto accept_result = client.AcceptFriendRequest(accept_request, options);

    tinyimx::rpc::RejectFriendRequestRpcRequest reject_request;
    reject_request.request_id = 7002;
    reject_request.handler_user_id = 10002;
    const auto reject_result = client.RejectFriendRequest(reject_request, options);

    const bool ownership_ok =
        Expect(permission_result.ok() && permission_result.value->Allowed(),
               "SocialService.ChatPermissionVerticalSlice") &&
        Expect(create_result.ok() && create_result.attempted &&
                   create_result.value->Success() && create_result.value->Changed() &&
                   create_result.value->request_id == 7001,
               "SocialService.CreateFriendRequestVerticalSlice") &&
        Expect(list_result.ok() && list_result.value->Succeeded() &&
                   list_result.value->requests.size() == 1,
               "SocialService.ListFriendRequestsVerticalSlice") &&
        Expect(accept_result.ok() && accept_result.value->Success() &&
                   !accept_result.value->Changed(),
               "SocialService.AcceptFriendRequestIdempotentOutcome") &&
        Expect(reject_result.ok() && reject_result.value->Success() &&
                   !reject_result.value->Changed(),
               "SocialService.RejectFriendRequestIdempotentOutcome");

    server.Shutdown();
    server.Wait();
    return ok && ownership_ok;
}

}  // namespace

int main() {
    std::cout
        << "========== TinyIMX M14-A3 SocialService Integration Tests ==========\n";

    const bool ok = TestRealGrpcVerticalSlice();
    const std::size_t failed = ok ? 0 : 1;

    std::cout
        << "===================================================================\n"
        << "total = 1, failed = " << failed << '\n';

    return ok ? 0 : 1;
}
