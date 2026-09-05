#include "services/rpc/RpcCallOptions.h"
#include "services/rpc/SocialRpcClient.h"
#include "services/rpc/StaticServiceEndpointProvider.h"
#include "services/social/application/FriendApplicationService.h"
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

    std::size_t call_count{0};
    std::uint64_t last_user_id{0};
    std::size_t last_limit{0};
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
    tinyimx::social::FriendApplicationService application(&repository);
    tinyimx::social::SocialServiceImpl service_impl(&application);
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

    server.Shutdown();
    server.Wait();
    return ok;
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
