#include "services/rpc/MessageRpcClient.h"
#include "services/rpc/SocialRpcClient.h"
#include "services/rpc/UserRpcClient.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using tinyimx::rpc::MessageRpcClient;
using tinyimx::rpc::ServiceEndpoint;
using tinyimx::rpc::ServiceEndpointProvider;
using tinyimx::rpc::ServiceKind;
using tinyimx::rpc::SocialRpcClient;
using tinyimx::rpc::UserRpcClient;

class SequenceEndpointProvider final : public ServiceEndpointProvider {
public:
    explicit SequenceEndpointProvider(std::vector<std::string> targets)
        : targets_(std::move(targets)) {
    }

    std::optional<ServiceEndpoint> Resolve(ServiceKind) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (targets_.empty()) {
            return std::nullopt;
        }
        const std::string& target = targets_[cursor_ % targets_.size()];
        ++cursor_;
        return ServiceEndpoint{target};
    }

private:
    std::vector<std::string> targets_;
    mutable std::mutex mutex_;
    mutable std::size_t cursor_{0};
};

bool Expect(bool condition, const std::string& name) {
    if (!condition) {
        std::cerr << "[FAIL] " << name << '\n';
        return false;
    }
    std::cout << "[PASS] " << name << '\n';
    return true;
}

tinyimx::rpc::RpcCallOptions Options() {
    tinyimx::rpc::RpcCallOptions options;
    options.request_id = "m15-b-cache";
    options.trace_id = "m15-b-cache";
    options.caller_service = "test";
    options.caller_instance = "test-1";
    options.remaining_timeout = std::chrono::milliseconds(5);
    return options;
}

void Call(UserRpcClient* client) {
    tinyimx::rpc::AuthenticateRpcRequest request;
    request.username = "nobody";
    request.password = "invalid";
    (void)client->Authenticate(request, Options());
}

void Call(SocialRpcClient* client) {
    tinyimx::rpc::ListFriendsRpcRequest request;
    request.actor_user_id = 1;
    request.limit = 1;
    (void)client->ListFriends(request, Options());
}

void Call(MessageRpcClient* client) {
    tinyimx::rpc::CountPendingRpcRequest request;
    request.to_user_id = 1;
    (void)client->CountPending(request, Options());
}

template <typename Client>
bool TestReuse(const std::string& name) {
    auto provider = std::make_shared<SequenceEndpointProvider>(
        std::vector<std::string>{
            "127.0.0.1:62991",
            "127.0.0.1:62992",
            "127.0.0.1:62991",
        }
    );
    Client client(provider);
    Call(&client);
    Call(&client);
    Call(&client);
    return
        Expect(client.CachedTargetCountForTest() == 2, name + " caches two targets") &&
        Expect(client.StubCreationCountForTest() == 2, name + " reuses A after A-B-A");
}

template <typename Client>
bool TestBounded(const std::string& name) {
    std::vector<std::string> targets;
    for (int index = 0; index < 20; ++index) {
        targets.push_back(
            "127.0.0.1:" + std::to_string(63000 + index)
        );
    }
    auto provider = std::make_shared<SequenceEndpointProvider>(targets);
    Client client(provider);
    for (int index = 0; index < 20; ++index) {
        Call(&client);
    }
    return
        Expect(
            client.CachedTargetCountForTest() == 16,
            name + " cache is bounded at 16 targets"
        ) &&
        Expect(
            client.StubCreationCountForTest() == 20,
            name + " created each unique target once"
        );
}

}  // namespace

int main() {
    std::cout
        << "========== TinyIMX M15-B RPC Multi-Target Cache Tests =========="
        << '\n';
    const bool ok =
        TestReuse<UserRpcClient>("UserRpcClient") &&
        TestReuse<SocialRpcClient>("SocialRpcClient") &&
        TestReuse<MessageRpcClient>("MessageRpcClient") &&
        TestBounded<UserRpcClient>("UserRpcClient") &&
        TestBounded<SocialRpcClient>("SocialRpcClient") &&
        TestBounded<MessageRpcClient>("MessageRpcClient");
    std::cout
        << "================================================================="
        << '\n';
    if (!ok) {
        return 1;
    }
    std::cout << "[PASS] M15-B RPC multi-target cache tests\n";
    return 0;
}
