#include "services/intelligence/mcp/McpDispatcher.h"
#include "services/intelligence/mcp/McpDomainTools.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
using tinyimx::mcp::Json;

class FakeBackend final : public tinyimx::mcp::DomainBackend {
public:
    mutable std::uint64_t last_actor{0};
    mutable std::uint64_t last_peer{0};
    mutable std::uint64_t last_file{0};
    mutable bool fail_conversations{false};

    tinyimx::rpc::RpcResult<tinyimx::rpc::GetUserProfileRpcResponse> GetSelfProfile(
        std::uint64_t actor, const tinyimx::rpc::RpcCallOptions&) const override {
        last_actor = actor;
        tinyimx::rpc::GetUserProfileRpcResponse out;
        out.profile = {actor, "alice", "Alice", "avatar", 1};
        return tinyimx::rpc::RpcResult<tinyimx::rpc::GetUserProfileRpcResponse>::Success(std::move(out));
    }

    tinyimx::rpc::RpcResult<tinyimx::rpc::ListFriendsRpcResponse> ListFriends(
        const tinyimx::rpc::ListFriendsRpcRequest& r,
        const tinyimx::rpc::RpcCallOptions&) const override {
        last_actor = r.actor_user_id;
        tinyimx::rpc::ListFriendsRpcResponse out;
        out.friends.push_back({77, "bob", "Bob", "avatar-bob", 1, 1, "2026-01-01", "2026-01-02"});
        return tinyimx::rpc::RpcResult<tinyimx::rpc::ListFriendsRpcResponse>::Success(std::move(out));
    }

    tinyimx::rpc::RpcResult<tinyimx::rpc::ListConversationsRpcResponse> ListConversations(
        const tinyimx::rpc::ListConversationsRpcRequest& r,
        const tinyimx::rpc::RpcCallOptions&) const override {
        last_actor = r.actor_user_id;
        if (fail_conversations) {
            return tinyimx::rpc::RpcResult<tinyimx::rpc::ListConversationsRpcResponse>::Failure(
                tinyimx::rpc::RpcErrorCode::kUnavailable, "message service unavailable");
        }
        tinyimx::rpc::ListConversationsRpcResponse out;
        tinyimx::rpc::ConversationRpcRecord item;
        item.peer_user_id = 77;
        item.last_message_id = 9001;
        item.last_client_message_id = "client-9001";
        item.last_from_user_id = r.actor_user_id;
        item.last_to_user_id = 77;
        item.last_message_type = 1;
        item.last_content = "hello";
        item.last_created_at = "2026-01-03";
        out.conversations.push_back(std::move(item));
        return tinyimx::rpc::RpcResult<tinyimx::rpc::ListConversationsRpcResponse>::Success(std::move(out));
    }

    tinyimx::rpc::RpcResult<tinyimx::rpc::ListHistoryRpcResponse> ListHistory(
        const tinyimx::rpc::ListHistoryRpcRequest& r,
        const tinyimx::rpc::RpcCallOptions&) const override {
        last_actor = r.actor_user_id; last_peer = r.peer_user_id;
        tinyimx::rpc::ListHistoryRpcResponse out;
        tinyimx::rpc::MessageRpcRecord item;
        item.message_id = 9002;
        item.client_message_id = "client-9002";
        item.from_user_id = r.actor_user_id;
        item.to_user_id = r.peer_user_id;
        item.message_type = 1;
        item.content = "history";
        item.created_at = "2026-01-04";
        out.messages.push_back(std::move(item));
        return tinyimx::rpc::RpcResult<tinyimx::rpc::ListHistoryRpcResponse>::Success(std::move(out));
    }

    tinyimx::rpc::RpcResult<tinyimx::rpc::GetGroupRpcResponse> GetGroup(
        const tinyimx::rpc::GetGroupRpcRequest& r,
        const tinyimx::rpc::RpcCallOptions&) const override {
        last_actor = r.actor_user_id;
        tinyimx::rpc::GetGroupRpcResponse out;
        out.group.group_id = r.group_id;
        out.group.name = "group-name";
        out.group.description = "group-description";
        out.group.owner_user_id = r.actor_user_id;
        return tinyimx::rpc::RpcResult<tinyimx::rpc::GetGroupRpcResponse>::Success(std::move(out));
    }

    tinyimx::rpc::RpcResult<tinyimx::rpc::ListMyGroupsRpcResponse> ListMyGroups(
        const tinyimx::rpc::ListMyGroupsRpcRequest& r,
        const tinyimx::rpc::RpcCallOptions&) const override {
        last_actor = r.actor_user_id;
        tinyimx::rpc::ListMyGroupsRpcResponse out;
        tinyimx::rpc::GroupRpcView group;
        group.group_id = 123;
        group.name = "my-group";
        group.owner_user_id = r.actor_user_id;
        out.groups.push_back(std::move(group));
        return tinyimx::rpc::RpcResult<tinyimx::rpc::ListMyGroupsRpcResponse>::Success(std::move(out));
    }

    tinyimx::rpc::RpcResult<tinyimx::rpc::ListGroupMembersRpcResponse> ListGroupMembers(
        const tinyimx::rpc::ListGroupMembersRpcRequest& r,
        const tinyimx::rpc::RpcCallOptions&) const override {
        last_actor = r.actor_user_id;
        tinyimx::rpc::ListGroupMembersRpcResponse out;
        tinyimx::rpc::GroupMemberRpcView member;
        member.group_id = r.group_id;
        member.user_id = 77;
        out.members.push_back(std::move(member));
        return tinyimx::rpc::RpcResult<tinyimx::rpc::ListGroupMembersRpcResponse>::Success(std::move(out));
    }

    tinyimx::rpc::RpcResult<tinyimx::rpc::GetDownloadInfoRpcResponse> GetFileMetadata(
        const tinyimx::rpc::GetDownloadInfoRpcRequest& r,
        const tinyimx::rpc::RpcCallOptions&) const override {
        last_actor = r.actor_user_id; last_file = r.file_id;
        tinyimx::rpc::GetDownloadInfoRpcResponse out;
        out.info.file_id = r.file_id;
        out.info.file_name = "a.txt";
        out.info.content_type = "text/plain";
        out.info.total_size = 10;
        out.info.checksum_algorithm = "sha256";
        out.info.verified_checksum = std::string(64, 'a');
        out.info.version = 1;
        out.info.available_at = "2026-01-05";
        out.message = "available";
        return tinyimx::rpc::RpcResult<tinyimx::rpc::GetDownloadInfoRpcResponse>::Success(std::move(out));
    }
};

bool Check(bool condition, const char* message) {
    if (!condition) std::cerr << "CHECK failed: " << message << '\n';
    return condition;
}

Json Call(tinyimx::mcp::Dispatcher& dispatcher, const tinyimx::mcp::RequestContext& ctx,
          const std::string& name, Json args, int id = 1) {
    tinyimx::mcp::DispatchMetadata metadata;
    metadata.protocol_version = tinyimx::mcp::kProtocolVersion;
    metadata.method_header = "tools/call";
    metadata.name_header = name;
    Json request{{"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/call"},
                 {"params", Json{{"name", name}, {"arguments", std::move(args)},
                                 {"_meta", Json{{"io.modelcontextprotocol/protocolVersion", tinyimx::mcp::kProtocolVersion}}}}}};
    return dispatcher.Dispatch(ctx, metadata, request).body;
}

}  // namespace

int main() {
    auto backend = std::make_shared<FakeBackend>();
    auto registry = std::make_shared<tinyimx::mcp::Registry>();
    std::string error;
    if (!Check(tinyimx::mcp::RegisterReadOnlyDomainTools(registry.get(), backend, &error), "register domain tools")) return 1;
    if (!Check(registry->Freeze(&error), "freeze registry")) return 1;

    const auto tools = registry->ListTools();
    if (!Check(tools.size() == 8, "eight read-only domain tools")) return 1;
    std::vector<std::string> names;
    for (const auto& item : tools) names.push_back(item.at("name").get<std::string>());
    if (!Check(std::is_sorted(names.begin(), names.end()), "deterministic tool order")) return 1;

    tinyimx::mcp::RequestContext ctx;
    ctx.principal.user_id = 42;
    ctx.principal.subject = "user:42";
    ctx.principal.scopes.insert("mcp.read");
    ctx.request_id = "r1";
    ctx.trace_id = "trace-1";
    ctx.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);

    tinyimx::mcp::Dispatcher dispatcher(registry);

    auto self = Call(dispatcher, ctx, "tinyimx.user.get_self_profile", Json::object(), 10);
    if (!Check(self["result"]["isError"] == false, "self profile succeeds")) return 1;
    if (!Check(self["result"]["structuredContent"]["profile"]["user_id"] == 42, "principal user id injected")) return 1;
    if (!Check(backend->last_actor == 42, "backend receives authenticated actor")) return 1;

    backend->last_actor = 0;
    auto spoof = Call(dispatcher, ctx, "tinyimx.user.get_self_profile", Json{{"user_id", 999}}, 11);
    if (!Check(spoof["result"]["isError"] == true, "spoofed actor argument rejected")) return 1;
    if (!Check(spoof["result"]["structuredContent"]["error"]["code"] == "invalid_arguments", "spoof rejection code")) return 1;
    if (!Check(backend->last_actor == 0, "backend not called for spoofed actor")) return 1;

    auto history = Call(dispatcher, ctx, "tinyimx.message.list_history",
                        Json{{"peer_user_id", 77}, {"limit", 20}}, 12);
    if (!Check(history["result"]["isError"] == false, "history succeeds")) return 1;
    if (!Check(backend->last_actor == 42 && backend->last_peer == 77, "history actor/peer boundary")) return 1;
    if (!Check(history["result"]["structuredContent"]["messages"][0]["content"] == "history",
               "history response lifetime")) return 1;

    auto friends = Call(dispatcher, ctx, "tinyimx.social.list_friends", Json{{"limit", 10}}, 15);
    if (!Check(friends["result"]["isError"] == false, "friends succeeds")) return 1;
    if (!Check(friends["result"]["structuredContent"]["friends"][0]["username"] == "bob",
               "friends response lifetime")) return 1;

    auto conversations = Call(dispatcher, ctx, "tinyimx.message.list_conversations", Json{{"limit", 10}}, 16);
    if (!Check(conversations["result"]["isError"] == false, "conversations succeeds")) return 1;
    if (!Check(conversations["result"]["structuredContent"]["conversations"][0]["last_content"] == "hello",
               "conversation response lifetime")) return 1;

    auto group = Call(dispatcher, ctx, "tinyimx.group.get", Json{{"group_id", 123}}, 17);
    if (!Check(group["result"]["isError"] == false, "group succeeds")) return 1;
    if (!Check(group["result"]["structuredContent"]["group"]["name"] == "group-name",
               "group response lifetime")) return 1;

    auto my_groups = Call(dispatcher, ctx, "tinyimx.group.list_my_groups", Json{{"limit", 10}}, 18);
    if (!Check(my_groups["result"]["isError"] == false, "my groups succeeds")) return 1;
    if (!Check(my_groups["result"]["structuredContent"]["groups"][0]["name"] == "my-group",
               "my groups response lifetime")) return 1;

    auto members = Call(dispatcher, ctx, "tinyimx.group.list_members",
                        Json{{"group_id", 123}, {"limit", 10}}, 19);
    if (!Check(members["result"]["isError"] == false, "group members succeeds")) return 1;
    if (!Check(members["result"]["structuredContent"]["members"][0]["user_id"] == 77,
               "group members response lifetime")) return 1;

    auto file = Call(dispatcher, ctx, "tinyimx.file.get_metadata", Json{{"file_id", 88}}, 13);
    if (!Check(file["result"]["isError"] == false, "file metadata succeeds")) return 1;
    if (!Check(backend->last_actor == 42 && backend->last_file == 88, "file actor boundary")) return 1;
    if (!Check(file["result"]["structuredContent"]["file"]["file_name"] == "a.txt",
               "file response lifetime")) return 1;
    if (!Check(file["result"]["structuredContent"]["message"] == "available",
               "file response message lifetime")) return 1;

    backend->fail_conversations = true;
    auto unavailable = Call(dispatcher, ctx, "tinyimx.message.list_conversations", Json::object(), 14);
    if (!Check(unavailable["result"]["isError"] == true, "RPC failure becomes tool error")) return 1;
    if (!Check(unavailable["result"]["structuredContent"]["error"]["code"] == "unavailable", "RPC error code mapping")) return 1;

    std::cout << "M19_MCP_DOMAIN_TOOLS_TESTS=PASS\n";
    return 0;
}
