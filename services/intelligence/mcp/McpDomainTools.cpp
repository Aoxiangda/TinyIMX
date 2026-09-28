#include "services/intelligence/mcp/McpDomainTools.h"


#include <algorithm>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

namespace tinyimx::mcp {
namespace {

constexpr std::uint32_t kDefaultPageSize = 50;
constexpr std::uint32_t kMaxPageSize = 100;

[[noreturn]] void Invalid(std::string message) {
    throw ToolExecutionError("invalid_arguments", std::move(message));
}

void EnsureObjectWithKeys(
    const Json& args,
    std::initializer_list<const char*> allowed
) {
    if (!args.is_object()) {
        Invalid("tool arguments must be an object");
    }
    std::unordered_set<std::string> keys;
    for (const char* key : allowed) keys.emplace(key);
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (keys.find(it.key()) == keys.end()) {
            Invalid("unsupported argument: " + it.key());
        }
    }
}

std::uint64_t NonNegativeU64Value(const Json& value, const char* key) {
    if (value.is_number_unsigned()) return value.get<std::uint64_t>();
    if (!value.is_number_integer()) {
        Invalid(std::string(key) + " must be an integer");
    }
    const auto signed_value = value.get<std::int64_t>();
    if (signed_value < 0) Invalid(std::string(key) + " must be non-negative");
    return static_cast<std::uint64_t>(signed_value);
}

std::uint64_t RequiredU64(const Json& args, const char* key) {
    if (!args.contains(key)) Invalid(std::string(key) + " is required");
    const auto value = NonNegativeU64Value(args.at(key), key);
    if (value == 0) Invalid(std::string(key) + " must be greater than zero");
    return value;
}

std::uint64_t OptionalU64(const Json& args, const char* key) {
    if (!args.contains(key)) return 0;
    return NonNegativeU64Value(args.at(key), key);
}

std::uint32_t PageSize(const Json& args) {
    if (!args.contains("limit")) return kDefaultPageSize;
    const auto value = NonNegativeU64Value(args.at("limit"), "limit");
    if (value == 0 || value > kMaxPageSize) {
        Invalid("limit must be in range 1-100");
    }
    return static_cast<std::uint32_t>(value);
}

rpc::RpcCallOptions RpcOptions(const RequestContext& context) {
    rpc::RpcCallOptions options;
    options.request_id = context.request_id;
    options.trace_id = context.trace_id;
    options.caller_service = "tinyimx-mcp-server";
    options.caller_instance = "mcp-server";

    const auto now = std::chrono::steady_clock::now();
    if (context.deadline == std::chrono::steady_clock::time_point{} ||
        context.deadline <= now) {
        throw ToolExecutionError("deadline_exceeded", "MCP request budget is exhausted");
    }
    options.remaining_timeout = std::max(
        std::chrono::milliseconds(1),
        std::chrono::duration_cast<std::chrono::milliseconds>(context.deadline - now)
    );
    return options;
}

const char* RpcErrorName(rpc::RpcErrorCode code) {
    using E = rpc::RpcErrorCode;
    switch (code) {
        case E::kInvalidArgument: return "invalid_argument";
        case E::kCancelled: return "cancelled";
        case E::kDeadlineExceeded: return "deadline_exceeded";
        case E::kNotFound: return "not_found";
        case E::kAlreadyExists: return "already_exists";
        case E::kPermissionDenied: return "permission_denied";
        case E::kUnauthenticated: return "unauthenticated";
        case E::kResourceExhausted: return "resource_exhausted";
        case E::kFailedPrecondition: return "failed_precondition";
        case E::kAborted: return "aborted";
        case E::kOutOfRange: return "out_of_range";
        case E::kUnimplemented: return "unimplemented";
        case E::kUnavailable: return "unavailable";
        case E::kInternal: return "internal";
        case E::kDataLoss: return "data_loss";
        case E::kUnknown: return "unknown";
        case E::kOk: return "ok";
    }
    return "unknown";
}

template <typename T>
T RequireRpc(rpc::RpcResult<T> result) {
    if (!result.ok()) {
        throw ToolExecutionError(
            RpcErrorName(result.status.code),
            result.status.message.empty() ? "domain service request failed" : result.status.message
        );
    }
    return std::move(*result.value);
}

Json ProfileJson(const rpc::UserProfileRpcView& p) {
    return Json{{"user_id", p.user_id}, {"username", p.username},
                {"nickname", p.nickname}, {"avatar_url", p.avatar_url},
                {"user_status", p.user_status}};
}

Json FriendJson(const rpc::SocialFriend& f) {
    return Json{{"friend_user_id", f.friend_user_id}, {"username", f.username},
                {"nickname", f.nickname}, {"avatar_url", f.avatar_url},
                {"user_status", f.user_status}, {"relation_status", f.relation_status},
                {"relation_created_at", f.relation_created_at},
                {"relation_updated_at", f.relation_updated_at}};
}

const char* DeliveryStateName(rpc::MessageDeliveryState state) {
    using S = rpc::MessageDeliveryState;
    switch (state) {
        case S::kPending: return "pending";
        case S::kReceiverConfirmed: return "receiver_confirmed";
        case S::kRead: return "read";
        case S::kFailed: return "failed";
    }
    return "unknown";
}

Json MessageJson(const rpc::MessageRpcRecord& m) {
    return Json{{"message_id", m.message_id}, {"client_message_id", m.client_message_id},
                {"from_user_id", m.from_user_id}, {"to_user_id", m.to_user_id},
                {"message_type", m.message_type}, {"content", m.content},
                {"delivery_state", DeliveryStateName(m.delivery_state)},
                {"created_at", m.created_at},
                {"receiver_confirmed_at", m.receiver_confirmed_at}, {"read_at", m.read_at}};
}

Json ConversationJson(const rpc::ConversationRpcRecord& c) {
    return Json{{"peer_user_id", c.peer_user_id}, {"last_message_id", c.last_message_id},
                {"last_client_message_id", c.last_client_message_id},
                {"last_from_user_id", c.last_from_user_id}, {"last_to_user_id", c.last_to_user_id},
                {"last_message_type", c.last_message_type}, {"last_content", c.last_content},
                {"last_delivery_state", DeliveryStateName(c.last_delivery_state)},
                {"last_created_at", c.last_created_at},
                {"last_receiver_confirmed_at", c.last_receiver_confirmed_at},
                {"last_read_at", c.last_read_at}};
}

const char* GroupStatusName(rpc::GroupRpcStatus value) {
    return value == rpc::GroupRpcStatus::kActive ? "active" : "disbanded";
}

const char* JoinPolicyName(rpc::GroupRpcJoinPolicy value) {
    return value == rpc::GroupRpcJoinPolicy::kOpen ? "open" : "invite_only";
}

const char* RoleName(rpc::GroupRpcRole value) {
    using R = rpc::GroupRpcRole;
    switch (value) {
        case R::kOwner: return "owner";
        case R::kAdmin: return "admin";
        case R::kMember: return "member";
    }
    return "unknown";
}

const char* MemberStatusName(rpc::GroupRpcMemberStatus value) {
    using S = rpc::GroupRpcMemberStatus;
    switch (value) {
        case S::kActive: return "active";
        case S::kLeft: return "left";
        case S::kKicked: return "kicked";
    }
    return "unknown";
}

Json GroupJson(const rpc::GroupRpcView& g) {
    return Json{{"group_id", g.group_id}, {"name", g.name}, {"description", g.description},
                {"avatar_url", g.avatar_url}, {"owner_user_id", g.owner_user_id},
                {"status", GroupStatusName(g.status)}, {"join_policy", JoinPolicyName(g.join_policy)},
                {"max_members", g.max_members}, {"version", g.version},
                {"member_version", g.member_version}, {"created_at", g.created_at},
                {"updated_at", g.updated_at}};
}

Json GroupMemberJson(const rpc::GroupMemberRpcView& m) {
    return Json{{"group_id", m.group_id}, {"user_id", m.user_id}, {"role", RoleName(m.role)},
                {"status", MemberStatusName(m.status)}, {"membership_epoch", m.membership_epoch},
                {"muted_until", m.muted_until}, {"joined_at", m.joined_at},
                {"left_at", m.left_at}, {"updated_at", m.updated_at}};
}

Json DownloadJson(const rpc::DownloadInfoRpcView& f) {
    return Json{{"file_id", f.file_id}, {"file_name", f.file_name},
                {"content_type", f.content_type}, {"total_size", f.total_size},
                {"checksum_algorithm", f.checksum_algorithm},
                {"verified_checksum", f.verified_checksum}, {"version", f.version},
                {"available_at", f.available_at}};
}

Json ObjectSchema(Json properties, Json required = Json::array()) {
    Json schema{{"type", "object"}, {"properties", std::move(properties)},
                {"additionalProperties", false}};
    if (!required.empty()) schema["required"] = std::move(required);
    return schema;
}

Json PositiveIdSchema() { return Json{{"type", "integer"}, {"minimum", 1}}; }
Json CursorSchema() { return Json{{"type", "integer"}, {"minimum", 0}}; }
Json LimitSchema() { return Json{{"type", "integer"}, {"minimum", 1}, {"maximum", kMaxPageSize}}; }

bool RegisterOne(Registry* registry, ToolDefinition def, ToolHandler handler, std::string* error) {
    if (!registry->RegisterTool(std::move(def), std::move(handler), error)) return false;
    return true;
}

}  // namespace

bool RegisterReadOnlyDomainTools(
    Registry* registry,
    std::shared_ptr<DomainBackend> backend,
    std::string* error
) {
    if (registry == nullptr || !backend) {
        if (error) *error = "domain tool registry/backend is null";
        return false;
    }
    const std::vector<std::string> scopes{"mcp.read"};

    if (!RegisterOne(registry,
        {"tinyimx.user.get_self_profile", "My profile",
         "Return the authenticated TinyIMX user's own profile. Actor identity is injected by MCPServer and cannot be supplied by tool arguments.",
         ObjectSchema(Json::object()), scopes},
        [backend](const RequestContext& ctx, const Json& args) {
            EnsureObjectWithKeys(args, {});
            auto response = RequireRpc(backend->GetSelfProfile(ctx.principal.user_id, RpcOptions(ctx)));
            return Json{{"profile", ProfileJson(response.profile)}};
        }, error)) return false;

    if (!RegisterOne(registry,
        {"tinyimx.social.list_friends", "List friends", "List friends for the authenticated TinyIMX user.",
         ObjectSchema(Json{{"limit", LimitSchema()}}), scopes},
        [backend](const RequestContext& ctx, const Json& args) {
            EnsureObjectWithKeys(args, {"limit"});
            rpc::ListFriendsRpcRequest request{ctx.principal.user_id, PageSize(args)};
            auto response = RequireRpc(backend->ListFriends(request, RpcOptions(ctx)));
            Json friends = Json::array();
            for (const auto& f : response.friends) friends.push_back(FriendJson(f));
            return Json{{"friends", std::move(friends)}, {"has_more", response.has_more}};
        }, error)) return false;

    if (!RegisterOne(registry,
        {"tinyimx.message.list_conversations", "List conversations", "List recent private conversations for the authenticated TinyIMX user.",
         ObjectSchema(Json{{"limit", LimitSchema()}}), scopes},
        [backend](const RequestContext& ctx, const Json& args) {
            EnsureObjectWithKeys(args, {"limit"});
            rpc::ListConversationsRpcRequest request{ctx.principal.user_id, PageSize(args)};
            auto response = RequireRpc(backend->ListConversations(request, RpcOptions(ctx)));
            Json items = Json::array();
            for (const auto& c : response.conversations) items.push_back(ConversationJson(c));
            return Json{{"conversations", std::move(items)}, {"has_more", response.has_more}};
        }, error)) return false;

    if (!RegisterOne(registry,
        {"tinyimx.message.list_history", "List message history", "List private message history between the authenticated TinyIMX user and one peer.",
         ObjectSchema(Json{{"peer_user_id", PositiveIdSchema()}, {"before_message_id", CursorSchema()}, {"limit", LimitSchema()}},
                      Json::array({"peer_user_id"})), scopes},
        [backend](const RequestContext& ctx, const Json& args) {
            EnsureObjectWithKeys(args, {"peer_user_id", "before_message_id", "limit"});
            rpc::ListHistoryRpcRequest request;
            request.actor_user_id = ctx.principal.user_id;
            request.peer_user_id = RequiredU64(args, "peer_user_id");
            request.before_message_id = OptionalU64(args, "before_message_id");
            request.limit = PageSize(args);
            auto response = RequireRpc(backend->ListHistory(request, RpcOptions(ctx)));
            Json items = Json::array();
            for (const auto& m : response.messages) items.push_back(MessageJson(m));
            return Json{{"messages", std::move(items)}, {"has_more", response.has_more}};
        }, error)) return false;

    if (!RegisterOne(registry,
        {"tinyimx.group.get", "Get group", "Return a group visible to the authenticated TinyIMX user.",
         ObjectSchema(Json{{"group_id", PositiveIdSchema()}}, Json::array({"group_id"})), scopes},
        [backend](const RequestContext& ctx, const Json& args) {
            EnsureObjectWithKeys(args, {"group_id"});
            rpc::GetGroupRpcRequest request{ctx.principal.user_id, RequiredU64(args, "group_id")};
            auto response = RequireRpc(backend->GetGroup(request, RpcOptions(ctx)));
            return Json{{"group", GroupJson(response.group)}};
        }, error)) return false;

    if (!RegisterOne(registry,
        {"tinyimx.group.list_my_groups", "List my groups", "List active groups for the authenticated TinyIMX user.",
         ObjectSchema(Json{{"after_group_id", CursorSchema()}, {"limit", LimitSchema()}}), scopes},
        [backend](const RequestContext& ctx, const Json& args) {
            EnsureObjectWithKeys(args, {"after_group_id", "limit"});
            rpc::ListMyGroupsRpcRequest request{ctx.principal.user_id, OptionalU64(args, "after_group_id"), PageSize(args)};
            auto response = RequireRpc(backend->ListMyGroups(request, RpcOptions(ctx)));
            Json groups = Json::array();
            for (const auto& g : response.groups) groups.push_back(GroupJson(g));
            return Json{{"groups", std::move(groups)}, {"has_more", response.has_more}};
        }, error)) return false;

    if (!RegisterOne(registry,
        {"tinyimx.group.list_members", "List group members", "List active members of a group visible to the authenticated TinyIMX user.",
         ObjectSchema(Json{{"group_id", PositiveIdSchema()}, {"after_user_id", CursorSchema()}, {"limit", LimitSchema()}},
                      Json::array({"group_id"})), scopes},
        [backend](const RequestContext& ctx, const Json& args) {
            EnsureObjectWithKeys(args, {"group_id", "after_user_id", "limit"});
            rpc::ListGroupMembersRpcRequest request;
            request.actor_user_id = ctx.principal.user_id;
            request.group_id = RequiredU64(args, "group_id");
            request.after_user_id = OptionalU64(args, "after_user_id");
            request.limit = PageSize(args);
            auto response = RequireRpc(backend->ListGroupMembers(request, RpcOptions(ctx)));
            Json members = Json::array();
            for (const auto& m : response.members) members.push_back(GroupMemberJson(m));
            return Json{{"members", std::move(members)}, {"has_more", response.has_more}};
        }, error)) return false;

    if (!RegisterOne(registry,
        {"tinyimx.file.get_metadata", "Get file metadata", "Return authorized metadata for an AVAILABLE file owned by the authenticated TinyIMX user.",
         ObjectSchema(Json{{"file_id", PositiveIdSchema()}}, Json::array({"file_id"})), scopes},
        [backend](const RequestContext& ctx, const Json& args) {
            EnsureObjectWithKeys(args, {"file_id"});
            rpc::GetDownloadInfoRpcRequest request{ctx.principal.user_id, RequiredU64(args, "file_id")};
            auto response = RequireRpc(backend->GetFileMetadata(request, RpcOptions(ctx)));
            return Json{{"file", DownloadJson(response.info)}, {"message", response.message}};
        }, error)) return false;

    return true;
}

}  // namespace tinyimx::mcp
