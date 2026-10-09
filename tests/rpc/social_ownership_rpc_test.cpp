#include "services/rpc/SocialRpcClient.h"
#include "services/rpc/SocialRpcTypes.h"
#include "services/rpc/StaticServiceEndpointProvider.h"
#include "tests/concurrency/TestFramework.h"
#include "tinyimx/social/v1/social_service.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace {

using namespace std::chrono_literals;
using tinyimx::rpc::AcceptFriendRequestRpcRequest;
using tinyimx::rpc::CheckPrivateChatPermissionRpcRequest;
using tinyimx::rpc::CreateFriendRequestRpcRequest;
using tinyimx::rpc::ListPendingIncomingFriendRequestsRpcRequest;
using tinyimx::rpc::RejectFriendRequestRpcRequest;
using tinyimx::rpc::RpcCallOptions;
using tinyimx::rpc::SocialRpcClient;
using tinyimx::rpc::StaticServiceEndpointProvider;
using tinyimx::test::TestRunner;
using tinyimx::social::v1::SocialService;

class FakeSocialOwnershipService final : public SocialService::Service {
public:
    enum class CreateBehavior {
        kSuccess,
        kTransportFailure,
    };

    void SetCreateBehavior(CreateBehavior behavior) noexcept {
        create_behavior_ = behavior;
    }

    grpc::Status CheckPrivateChatPermission(
        grpc::ServerContext*,
        const tinyimx::social::v1::CheckPrivateChatPermissionRequest* request,
        tinyimx::social::v1::CheckPrivateChatPermissionResponse* response
    ) override {
        if (request == nullptr || response == nullptr) {
            return {grpc::StatusCode::INVALID_ARGUMENT, "bad request"};
        }
        response->set_result(
            tinyimx::social::v1::CHAT_PERMISSION_RESULT_ALLOWED);
        response->set_message("allowed");
        return grpc::Status::OK;
    }

    grpc::Status CreateFriendRequest(
        grpc::ServerContext*,
        const tinyimx::social::v1::CreateFriendRequestRequest* request,
        tinyimx::social::v1::CreateFriendRequestResponse* response
    ) override {
        if (request == nullptr || response == nullptr) {
            return {grpc::StatusCode::INVALID_ARGUMENT, "bad request"};
        }
        if (create_behavior_ == CreateBehavior::kTransportFailure) {
            return {grpc::StatusCode::DEADLINE_EXCEEDED,
                    "simulated response loss after attempt"};
        }
        response->set_result(
            tinyimx::social::v1::FRIEND_REQUEST_CREATE_RESULT_CREATED);
        response->set_request_id(7001);
        response->set_message("created");
        return grpc::Status::OK;
    }

    grpc::Status ListPendingIncomingFriendRequests(
        grpc::ServerContext*,
        const tinyimx::social::v1::ListPendingIncomingFriendRequestsRequest* request,
        tinyimx::social::v1::ListPendingIncomingFriendRequestsResponse* response
    ) override {
        if (request == nullptr || response == nullptr) {
            return {grpc::StatusCode::INVALID_ARGUMENT, "bad request"};
        }
        response->set_result(
            tinyimx::social::v1::FRIEND_REQUEST_LIST_RESULT_SUCCEEDED);
        auto* item = response->add_requests();
        item->set_request_id(8001);
        item->set_from_user_id(10001);
        item->set_to_user_id(request->receiver_user_id());
        item->set_request_status(0);
        item->set_created_at("2026-09-29 12:00:00");
        response->set_has_more(true);
        response->set_next_before_created_at("2026-09-29 12:00:00");
        response->set_next_before_request_id(8001);
        response->set_message("ok");
        return grpc::Status::OK;
    }

    grpc::Status AcceptFriendRequest(
        grpc::ServerContext*,
        const tinyimx::social::v1::AcceptFriendRequestRequest* request,
        tinyimx::social::v1::AcceptFriendRequestResponse* response
    ) override {
        if (request == nullptr || response == nullptr) {
            return {grpc::StatusCode::INVALID_ARGUMENT, "bad request"};
        }
        response->set_result(
            tinyimx::social::v1::FRIEND_REQUEST_ACCEPT_RESULT_ALREADY_ACCEPTED);
        response->set_request_id(request->request_id());
        response->set_receiver_user_id(request->handler_user_id());
        response->set_message("already accepted");
        return grpc::Status::OK;
    }

    grpc::Status RejectFriendRequest(
        grpc::ServerContext*,
        const tinyimx::social::v1::RejectFriendRequestRequest* request,
        tinyimx::social::v1::RejectFriendRequestResponse* response
    ) override {
        if (request == nullptr || response == nullptr) {
            return {grpc::StatusCode::INVALID_ARGUMENT, "bad request"};
        }
        response->set_result(
            tinyimx::social::v1::FRIEND_REQUEST_REJECT_RESULT_ALREADY_REJECTED);
        response->set_request_id(request->request_id());
        response->set_receiver_user_id(request->handler_user_id());
        response->set_message("already rejected");
        return grpc::Status::OK;
    }

private:
    CreateBehavior create_behavior_{CreateBehavior::kSuccess};
};

class Server final {
public:
    bool Start() {
        grpc::ServerBuilder builder;
        int port = 0;
        builder.AddListeningPort(
            "127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
        builder.RegisterService(&service_);
        server_ = builder.BuildAndStart();
        if (!server_ || port <= 0) return false;
        target_ = "127.0.0.1:" + std::to_string(port);
        return true;
    }
    ~Server() {
        if (server_) {
            server_->Shutdown();
            server_->Wait();
        }
    }
    const std::string& target() const noexcept { return target_; }
    FakeSocialOwnershipService& service() noexcept { return service_; }
private:
    FakeSocialOwnershipService service_;
    std::unique_ptr<grpc::Server> server_;
    std::string target_;
};

RpcCallOptions Options() {
    RpcCallOptions options;
    options.request_id = "p0-social-ownership-request";
    options.trace_id = "p0-social-ownership-trace";
    options.caller_service = "gateway-test";
    options.caller_instance = "gateway-a";
    options.remaining_timeout = 1s;
    return options;
}

void TestQueryAndMutationMapping() {
    Server server;
    TINYIMX_EXPECT_TRUE(server.Start());
    if (server.target().empty()) return;

    auto provider = std::make_shared<StaticServiceEndpointProvider>(server.target());
    SocialRpcClient client(provider);

    CheckPrivateChatPermissionRpcRequest permission;
    permission.from_user_id = 10001;
    permission.to_user_id = 10002;
    const auto permission_result = client.CheckPrivateChatPermission(permission, Options());
    TINYIMX_EXPECT_TRUE(permission_result.ok() && permission_result.value->Allowed());

    CreateFriendRequestRpcRequest create;
    create.from_user_id = 10001;
    create.to_user_id = 10002;
    create.request_message = "hello";
    const auto create_result = client.CreateFriendRequest(create, Options());
    TINYIMX_EXPECT_TRUE(create_result.ok() && create_result.attempted &&
                        create_result.value->Success() && create_result.value->Changed() &&
                        create_result.value->request_id == 7001);

    ListPendingIncomingFriendRequestsRpcRequest list;
    list.receiver_user_id = 10002;
    list.limit = 20;
    const auto list_result = client.ListPendingIncomingFriendRequests(list, Options());
    TINYIMX_EXPECT_TRUE(list_result.ok() && list_result.value->Succeeded() &&
                        list_result.value->requests.size() == 1 &&
                        list_result.value->has_more &&
                        list_result.value->next_before_request_id == 8001);

    AcceptFriendRequestRpcRequest accept;
    accept.request_id = 8001;
    accept.handler_user_id = 10002;
    const auto accept_result = client.AcceptFriendRequest(accept, Options());
    TINYIMX_EXPECT_TRUE(accept_result.ok() && accept_result.attempted &&
                        accept_result.value->Success() && !accept_result.value->Changed());

    RejectFriendRequestRpcRequest reject;
    reject.request_id = 8002;
    reject.handler_user_id = 10002;
    const auto reject_result = client.RejectFriendRequest(reject, Options());
    TINYIMX_EXPECT_TRUE(reject_result.ok() && reject_result.attempted &&
                        reject_result.value->Success() && !reject_result.value->Changed());
}

void TestAttemptedSemantics() {
    {
        auto provider = std::make_shared<StaticServiceEndpointProvider>("");
        SocialRpcClient client(provider);
        CreateFriendRequestRpcRequest request;
        request.from_user_id = 10001;
        request.to_user_id = 10002;
        const auto result = client.CreateFriendRequest(request, Options());
        TINYIMX_EXPECT_TRUE(!result.ok() && !result.attempted);
    }

    Server server;
    TINYIMX_EXPECT_TRUE(server.Start());
    if (server.target().empty()) return;
    server.service().SetCreateBehavior(
        FakeSocialOwnershipService::CreateBehavior::kTransportFailure);
    auto provider = std::make_shared<StaticServiceEndpointProvider>(server.target());
    SocialRpcClient client(provider);

    CreateFriendRequestRpcRequest request;
    request.from_user_id = 10001;
    request.to_user_id = 10002;
    const auto result = client.CreateFriendRequest(request, Options());
    TINYIMX_EXPECT_TRUE(!result.ok() && result.attempted &&
                        result.status.code == tinyimx::rpc::RpcErrorCode::kDeadlineExceeded);
}

}  // namespace

int main() {
    TestRunner runner;
    runner.Add("SocialOwnershipRpc.QueryAndMutationMapping", [] {
        TestQueryAndMutationMapping();
    });
    runner.Add("SocialOwnershipRpc.AttemptedSemantics", [] {
        TestAttemptedSemantics();
    });
    return runner.RunAll("TinyIMX P0 Social Ownership RPC Tests");
}
