#include "services/rpc/RpcCallOptions.h"
#include "services/rpc/ServiceEndpointProvider.h"
#include "services/rpc/SocialRpcClient.h"
#include "services/rpc/SocialRpcTypes.h"
#include "services/rpc/StaticServiceEndpointProvider.h"
#include "tests/concurrency/TestFramework.h"

#include "tinyimx/social/v1/social_service.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

using tinyimx::rpc::ListFriendsRpcRequest;
using tinyimx::rpc::RpcCallOptions;
using tinyimx::rpc::RpcErrorCode;
using tinyimx::rpc::ServiceEndpoint;
using tinyimx::rpc::ServiceEndpointProvider;
using tinyimx::rpc::ServiceKind;
using tinyimx::rpc::SocialRpcClient;
using tinyimx::rpc::StaticServiceEndpointProvider;
using tinyimx::social::v1::ListFriendsRequest;
using tinyimx::social::v1::ListFriendsResponse;
using tinyimx::social::v1::SocialService;
using tinyimx::test::TestRunner;

enum class FakeBehavior {
    kSuccess,
    kInvalidArgument,
    kResourceExhausted,
    kUnavailable,
    kSlowSuccess,
};

struct CapturedRequest {
    std::string request_id;
    std::string trace_id;
    std::string caller_service;
    std::string caller_instance;
    std::uint64_t actor_user_id{0};
    std::uint32_t limit{0};
};

class FakeSocialService final
    : public SocialService::Service {
public:
    FakeSocialService(
        FakeBehavior behavior,
        std::string friend_username,
        std::chrono::milliseconds delay = 0ms
    )
        : behavior_(behavior),
          friend_username_(std::move(friend_username)),
          delay_(delay) {
    }

    grpc::Status ListFriends(
        grpc::ServerContext*,
        const ListFriendsRequest* request,
        ListFriendsResponse* response
    ) override {
        call_count_.fetch_add(1, std::memory_order_relaxed);

        if (request != nullptr) {
            std::lock_guard<std::mutex> lock(capture_mutex_);
            captured_.request_id = request->meta().request_id();
            captured_.trace_id = request->meta().trace_id();
            captured_.caller_service = request->meta().caller_service();
            captured_.caller_instance = request->meta().caller_instance();
            captured_.actor_user_id = request->actor_user_id();
            captured_.limit = request->limit();
        }

        switch (behavior_) {
            case FakeBehavior::kInvalidArgument:
                return grpc::Status(
                    grpc::StatusCode::INVALID_ARGUMENT,
                    "fake invalid argument"
                );

            case FakeBehavior::kResourceExhausted:
                return grpc::Status(
                    grpc::StatusCode::RESOURCE_EXHAUSTED,
                    "fake overload"
                );

            case FakeBehavior::kUnavailable:
                return grpc::Status(
                    grpc::StatusCode::UNAVAILABLE,
                    "fake unavailable"
                );

            case FakeBehavior::kSlowSuccess:
                std::this_thread::sleep_for(delay_);
                break;

            case FakeBehavior::kSuccess:
                break;
        }

        if (response == nullptr) {
            return grpc::Status(
                grpc::StatusCode::INTERNAL,
                "response is null"
            );
        }

        auto* friend_info = response->add_friends();
        friend_info->set_friend_user_id(10002);
        friend_info->set_username(friend_username_);
        friend_info->set_nickname("friend-10002");
        friend_info->set_avatar_url("avatar://10002");
        friend_info->set_user_status(1);
        friend_info->set_relation_status(1);
        friend_info->set_relation_created_at("2026-09-03 10:00:00");
        friend_info->set_relation_updated_at("2026-09-03 10:00:00");
        response->set_has_more(true);

        return grpc::Status::OK;
    }

    [[nodiscard]] std::uint64_t call_count() const noexcept {
        return call_count_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] CapturedRequest captured() const {
        std::lock_guard<std::mutex> lock(capture_mutex_);
        return captured_;
    }

private:
    const FakeBehavior behavior_;
    const std::string friend_username_;
    const std::chrono::milliseconds delay_;

    std::atomic<std::uint64_t> call_count_{0};
    mutable std::mutex capture_mutex_;
    CapturedRequest captured_;
};

class FakeSocialServer {
public:
    FakeSocialServer(
        FakeBehavior behavior,
        std::string friend_username,
        std::chrono::milliseconds delay = 0ms
    )
        : service_(
              behavior,
              std::move(friend_username),
              delay
          ) {
    }

    ~FakeSocialServer() {
        Stop();
    }

    FakeSocialServer(const FakeSocialServer&) = delete;
    FakeSocialServer& operator=(const FakeSocialServer&) = delete;

    bool Start() {
        if (server_) {
            return true;
        }

        grpc::ServerBuilder builder;
        int selected_port = 0;

        builder.AddListeningPort(
            "127.0.0.1:0",
            grpc::InsecureServerCredentials(),
            &selected_port
        );
        builder.RegisterService(&service_);

        server_ = builder.BuildAndStart();

        if (!server_ || selected_port <= 0) {
            server_.reset();
            return false;
        }

        target_ =
            "127.0.0.1:" + std::to_string(selected_port);
        return true;
    }

    void Stop() {
        if (!server_) {
            return;
        }

        server_->Shutdown();
        server_->Wait();
        server_.reset();
    }

    [[nodiscard]] const std::string& target() const noexcept {
        return target_;
    }

    [[nodiscard]] FakeSocialService& service() noexcept {
        return service_;
    }

private:
    FakeSocialService service_;
    std::unique_ptr<grpc::Server> server_;
    std::string target_;
};

class MutableEndpointProvider final
    : public ServiceEndpointProvider {
public:
    explicit MutableEndpointProvider(std::string target)
        : target_(std::move(target)) {
    }

    void SetTarget(std::string target) {
        std::lock_guard<std::mutex> lock(mutex_);
        target_ = std::move(target);
    }

    std::optional<ServiceEndpoint> Resolve(
        ServiceKind service
    ) const override {
        if (service != ServiceKind::kSocial) {
            return std::nullopt;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (target_.empty()) {
            return std::nullopt;
        }

        return ServiceEndpoint{target_};
    }

private:
    mutable std::mutex mutex_;
    std::string target_;
};

RpcCallOptions MakeOptions(
    std::chrono::milliseconds timeout = 1s
) {
    RpcCallOptions options;
    options.request_id = "rpc-request-1";
    options.trace_id = "trace-1";
    options.caller_service = "gateway";
    options.caller_instance = "gateway-a";
    options.remaining_timeout = timeout;
    return options;
}

ListFriendsRpcRequest MakeRequest() {
    ListFriendsRpcRequest request;
    request.actor_user_id = 10001;
    request.limit = 100;
    return request;
}

void TestStaticEndpointResolve() {
    StaticServiceEndpointProvider provider("127.0.0.1:50051");

    const auto endpoint = provider.Resolve(ServiceKind::kSocial);

    TINYIMX_EXPECT_TRUE(endpoint.has_value());
    TINYIMX_EXPECT_EQ(endpoint->target, std::string("127.0.0.1:50051"));
}

void TestMissingEndpointFastFail() {
    auto provider =
        std::make_shared<StaticServiceEndpointProvider>("");
    SocialRpcClient client(provider);

    const auto result =
        client.ListFriends(MakeRequest(), MakeOptions());

    TINYIMX_EXPECT_TRUE(!result.ok());
    TINYIMX_EXPECT_EQ(result.status.code, RpcErrorCode::kUnavailable);
}

void TestInvalidActorFastFail() {
    FakeSocialServer server(FakeBehavior::kSuccess, "success-user");
    TINYIMX_EXPECT_TRUE(server.Start());

    auto provider =
        std::make_shared<StaticServiceEndpointProvider>(server.target());
    SocialRpcClient client(provider);

    auto request = MakeRequest();
    request.actor_user_id = 0;

    const auto result = client.ListFriends(request, MakeOptions());

    TINYIMX_EXPECT_EQ(result.status.code, RpcErrorCode::kInvalidArgument);
    TINYIMX_EXPECT_EQ(server.service().call_count(), std::uint64_t{0});
}

void TestZeroLimitFastFail() {
    FakeSocialServer server(FakeBehavior::kSuccess, "success-user");
    TINYIMX_EXPECT_TRUE(server.Start());

    auto provider =
        std::make_shared<StaticServiceEndpointProvider>(server.target());
    SocialRpcClient client(provider);

    auto request = MakeRequest();
    request.limit = 0;

    const auto result = client.ListFriends(request, MakeOptions());

    TINYIMX_EXPECT_EQ(result.status.code, RpcErrorCode::kInvalidArgument);
    TINYIMX_EXPECT_EQ(server.service().call_count(), std::uint64_t{0});
}

void TestExpiredBudgetFastFail() {
    FakeSocialServer server(FakeBehavior::kSuccess, "success-user");
    TINYIMX_EXPECT_TRUE(server.Start());

    auto provider =
        std::make_shared<StaticServiceEndpointProvider>(server.target());
    SocialRpcClient client(provider);

    const auto result =
        client.ListFriends(MakeRequest(), MakeOptions(0ms));

    TINYIMX_EXPECT_EQ(result.status.code, RpcErrorCode::kDeadlineExceeded);
    TINYIMX_EXPECT_EQ(server.service().call_count(), std::uint64_t{0});
}

void TestListFriendsSuccessAndMetadataPropagation() {
    FakeSocialServer server(FakeBehavior::kSuccess, "server-success");
    TINYIMX_EXPECT_TRUE(server.Start());

    auto provider =
        std::make_shared<StaticServiceEndpointProvider>(server.target());
    SocialRpcClient client(provider);

    const auto result =
        client.ListFriends(MakeRequest(), MakeOptions());

    TINYIMX_EXPECT_TRUE(result.ok());
    TINYIMX_EXPECT_TRUE(result.value.has_value());
    TINYIMX_EXPECT_EQ(result.value->friends.size(), std::size_t{1});
    TINYIMX_EXPECT_EQ(result.value->friends[0].friend_user_id, std::uint64_t{10002});
    TINYIMX_EXPECT_EQ(result.value->friends[0].username, std::string("server-success"));
    TINYIMX_EXPECT_TRUE(result.value->has_more);

    const auto captured = server.service().captured();
    TINYIMX_EXPECT_EQ(captured.request_id, std::string("rpc-request-1"));
    TINYIMX_EXPECT_EQ(captured.trace_id, std::string("trace-1"));
    TINYIMX_EXPECT_EQ(captured.caller_service, std::string("gateway"));
    TINYIMX_EXPECT_EQ(captured.caller_instance, std::string("gateway-a"));
    TINYIMX_EXPECT_EQ(captured.actor_user_id, std::uint64_t{10001});
    TINYIMX_EXPECT_EQ(captured.limit, std::uint32_t{100});
}

void TestInvalidArgumentMapping() {
    FakeSocialServer server(FakeBehavior::kInvalidArgument, "unused");
    TINYIMX_EXPECT_TRUE(server.Start());

    auto provider =
        std::make_shared<StaticServiceEndpointProvider>(server.target());
    SocialRpcClient client(provider);

    const auto result =
        client.ListFriends(MakeRequest(), MakeOptions());

    TINYIMX_EXPECT_EQ(result.status.code, RpcErrorCode::kInvalidArgument);
    TINYIMX_EXPECT_TRUE(!result.value.has_value());
}

void TestResourceExhaustedMapping() {
    FakeSocialServer server(FakeBehavior::kResourceExhausted, "unused");
    TINYIMX_EXPECT_TRUE(server.Start());

    auto provider =
        std::make_shared<StaticServiceEndpointProvider>(server.target());
    SocialRpcClient client(provider);

    const auto result =
        client.ListFriends(MakeRequest(), MakeOptions());

    TINYIMX_EXPECT_EQ(result.status.code, RpcErrorCode::kResourceExhausted);
}

void TestUnavailableMapping() {
    FakeSocialServer server(FakeBehavior::kUnavailable, "unused");
    TINYIMX_EXPECT_TRUE(server.Start());

    auto provider =
        std::make_shared<StaticServiceEndpointProvider>(server.target());
    SocialRpcClient client(provider);

    const auto result =
        client.ListFriends(MakeRequest(), MakeOptions());

    TINYIMX_EXPECT_EQ(result.status.code, RpcErrorCode::kUnavailable);
}

void TestDeadlineExceeded() {
    FakeSocialServer server(
        FakeBehavior::kSlowSuccess,
        "slow-user",
        200ms
    );
    TINYIMX_EXPECT_TRUE(server.Start());

    auto provider =
        std::make_shared<StaticServiceEndpointProvider>(server.target());
    SocialRpcClient client(provider);

    const auto result =
        client.ListFriends(MakeRequest(), MakeOptions(30ms));

    TINYIMX_EXPECT_EQ(result.status.code, RpcErrorCode::kDeadlineExceeded);
    TINYIMX_EXPECT_EQ(server.service().call_count(), std::uint64_t{1});
}

void TestConcurrentCallsShareClientSafely() {
    FakeSocialServer server(FakeBehavior::kSuccess, "concurrent-user");
    TINYIMX_EXPECT_TRUE(server.Start());

    auto provider =
        std::make_shared<StaticServiceEndpointProvider>(server.target());
    SocialRpcClient client(provider);

    constexpr int kThreads = 32;
    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&client, &success_count]() {
            auto request = MakeRequest();
            const auto result =
                client.ListFriends(request, MakeOptions(2s));

            if (result.ok() &&
                result.value.has_value() &&
                result.value->friends.size() == 1) {
                success_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    TINYIMX_EXPECT_EQ(success_count.load(), kThreads);
    TINYIMX_EXPECT_EQ(server.service().call_count(), std::uint64_t{kThreads});
}

void TestEndpointChangeRefreshesStub() {
    FakeSocialServer server_a(FakeBehavior::kSuccess, "server-a");
    FakeSocialServer server_b(FakeBehavior::kSuccess, "server-b");

    TINYIMX_EXPECT_TRUE(server_a.Start());
    TINYIMX_EXPECT_TRUE(server_b.Start());

    auto provider =
        std::make_shared<MutableEndpointProvider>(server_a.target());
    SocialRpcClient client(provider);

    const auto first =
        client.ListFriends(MakeRequest(), MakeOptions());

    TINYIMX_EXPECT_TRUE(first.ok());
    TINYIMX_EXPECT_EQ(
        first.value->friends[0].username,
        std::string("server-a")
    );

    provider->SetTarget(server_b.target());

    const auto second =
        client.ListFriends(MakeRequest(), MakeOptions());

    TINYIMX_EXPECT_TRUE(second.ok());
    TINYIMX_EXPECT_EQ(
        second.value->friends[0].username,
        std::string("server-b")
    );

    TINYIMX_EXPECT_EQ(server_a.service().call_count(), std::uint64_t{1});
    TINYIMX_EXPECT_EQ(server_b.service().call_count(), std::uint64_t{1});
}

}  // namespace

int main() {
    TestRunner runner;

    runner.Add("RpcClient.StaticEndpointResolve", TestStaticEndpointResolve);
    runner.Add("RpcClient.MissingEndpointFastFail", TestMissingEndpointFastFail);
    runner.Add("RpcClient.InvalidActorFastFail", TestInvalidActorFastFail);
    runner.Add("RpcClient.ZeroLimitFastFail", TestZeroLimitFastFail);
    runner.Add("RpcClient.ExpiredBudgetFastFail", TestExpiredBudgetFastFail);
    runner.Add(
        "RpcClient.ListFriendsSuccessAndMetadataPropagation",
        TestListFriendsSuccessAndMetadataPropagation
    );
    runner.Add("RpcClient.InvalidArgumentMapping", TestInvalidArgumentMapping);
    runner.Add("RpcClient.ResourceExhaustedMapping", TestResourceExhaustedMapping);
    runner.Add("RpcClient.UnavailableMapping", TestUnavailableMapping);
    runner.Add("RpcClient.DeadlineExceeded", TestDeadlineExceeded);
    runner.Add("RpcClient.ConcurrentCallsShareClientSafely", TestConcurrentCallsShareClientSafely);
    runner.Add("RpcClient.EndpointChangeRefreshesStub", TestEndpointChangeRefreshesStub);

    const int failed =
        runner.RunAll("TinyIMX M14-A2 RPC Client Tests");

    return failed == 0 ? 0 : 1;
}
