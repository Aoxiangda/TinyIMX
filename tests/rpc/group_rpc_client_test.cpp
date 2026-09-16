#include "services/rpc/GroupRpcClient.h"
#include "services/rpc/StaticServiceEndpointProvider.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>

namespace {

bool Expect(bool condition, const char* name) {
    if (condition) { std::cout << "[PASS] " << name << '\n'; return true; }
    std::cerr << "[FAIL] " << name << '\n'; return false;
}

tinyimx::rpc::RpcCallOptions Options(std::chrono::milliseconds timeout) {
    tinyimx::rpc::RpcCallOptions options;
    options.request_id = "m17-a3-group-client";
    options.trace_id = "m17-a3-group-client";
    options.caller_service = "test";
    options.caller_instance = "test-1";
    options.remaining_timeout = timeout;
    return options;
}

}  // namespace

int main() {
    using namespace tinyimx::rpc;
    bool ok = true;

    auto provider = std::make_shared<StaticServiceEndpointProvider>(
        "127.0.0.1:50051",
        "127.0.0.1:50052",
        "127.0.0.1:50053",
        "127.0.0.1:62994"
    );
    const auto group_endpoint = provider->Resolve(ServiceKind::kGroup);
    ok &= Expect(group_endpoint.has_value() && group_endpoint->target == "127.0.0.1:62994",
                 "StaticServiceEndpointProvider resolves Group target");

    GroupRpcClient client(provider);

    GetGroupRpcRequest invalid;
    const auto invalid_result = client.GetGroup(invalid, Options(std::chrono::milliseconds(10)));
    ok &= Expect(!invalid_result.ok() && invalid_result.status.code == RpcErrorCode::kInvalidArgument,
                 "GroupRpcClient rejects invalid identity before network");

    GetGroupRpcRequest valid{10001, 90001};
    const auto deadline_result = client.GetGroup(valid, Options(std::chrono::milliseconds(0)));
    ok &= Expect(!deadline_result.ok() && deadline_result.status.code == RpcErrorCode::kDeadlineExceeded,
                 "GroupRpcClient fails closed on exhausted E2E budget");

    auto no_group_provider = std::make_shared<StaticServiceEndpointProvider>(
        "127.0.0.1:50051", "127.0.0.1:50052", "127.0.0.1:50053"
    );
    GroupRpcClient no_group(no_group_provider);
    const auto unavailable = no_group.GetGroup(valid, Options(std::chrono::milliseconds(10)));
    ok &= Expect(!unavailable.ok() && unavailable.status.code == RpcErrorCode::kUnavailable,
                 "GroupRpcClient fails closed when Group endpoint missing");

    SetMemberRoleRpcRequest owner_role;
    owner_role.actor_user_id = 10001;
    owner_role.client_operation_id = "op-owner-role";
    owner_role.group_id = 90001;
    owner_role.target_user_id = 10002;
    owner_role.role = GroupRpcRole::kOwner;
    const auto owner_role_result = client.SetMemberRole(owner_role, Options(std::chrono::milliseconds(10)));
    ok &= Expect(!owner_role_result.ok() && owner_role_result.status.code == RpcErrorCode::kInvalidArgument,
                 "GroupRpcClient rejects OWNER through ordinary role mutation");

    std::cout << (ok ? "[PASS] M17-A3 GroupRpcClient contract tests\n"
                     : "[FAIL] M17-A3 GroupRpcClient contract tests\n");
    return ok ? 0 : 1;
}
