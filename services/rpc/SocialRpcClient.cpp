#include "services/rpc/SocialRpcClient.h"

#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace tinyimx::rpc {
namespace {

RpcResult<ListFriendsRpcResponse> Failure(
    RpcErrorCode code,
    std::string message
) {
    return RpcResult<ListFriendsRpcResponse>::Failure(
        code,
        std::move(message)
    );
}

}  // namespace

SocialRpcClient::SocialRpcClient(
    std::shared_ptr<const ServiceEndpointProvider>
        endpoint_provider
)
    : endpoint_provider_(std::move(endpoint_provider)) {
}

RpcResult<ListFriendsRpcResponse>
SocialRpcClient::ListFriends(
    const ListFriendsRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.actor_user_id == 0) {
        return Failure(
            RpcErrorCode::kInvalidArgument,
            "ListFriends actor_user_id must be non-zero"
        );
    }

    if (request.limit == 0) {
        return Failure(
            RpcErrorCode::kInvalidArgument,
            "ListFriends limit must be non-zero"
        );
    }

    if (options.remaining_timeout <=
        std::chrono::milliseconds::zero()) {
        return Failure(
            RpcErrorCode::kDeadlineExceeded,
            "ListFriends remaining RPC budget is exhausted"
        );
    }

    if (!endpoint_provider_) {
        return Failure(
            RpcErrorCode::kUnavailable,
            "SocialService endpoint provider is not configured"
        );
    }

    const auto endpoint =
        endpoint_provider_->Resolve(ServiceKind::kSocial);

    if (!endpoint.has_value() ||
        endpoint->target.empty()) {
        return Failure(
            RpcErrorCode::kUnavailable,
            "SocialService endpoint is unavailable"
        );
    }

    auto stub = GetOrCreateStub(*endpoint);

    if (!stub) {
        return Failure(
            RpcErrorCode::kUnavailable,
            "SocialService gRPC stub could not be created"
        );
    }

    tinyimx::social::v1::ListFriendsRequest proto_request;
    auto* meta = proto_request.mutable_meta();

    meta->set_request_id(options.request_id);
    meta->set_trace_id(options.trace_id);
    meta->set_caller_service(options.caller_service);
    meta->set_caller_instance(options.caller_instance);

    proto_request.set_actor_user_id(request.actor_user_id);
    proto_request.set_limit(request.limit);

    tinyimx::social::v1::ListFriendsResponse proto_response;
    grpc::ClientContext context;

    context.set_deadline(
        std::chrono::system_clock::now() +
        options.remaining_timeout
    );

    const grpc::Status grpc_status =
        stub->ListFriends(
            &context,
            proto_request,
            &proto_response
        );

    if (!grpc_status.ok()) {
        const RpcStatus mapped = MapGrpcStatus(grpc_status);
        return Failure(mapped.code, mapped.message);
    }

    ListFriendsRpcResponse response;
    response.has_more = proto_response.has_more();
    response.friends.reserve(
        static_cast<std::size_t>(
            proto_response.friends_size()
        )
    );

    for (const auto& proto_friend :
         proto_response.friends()) {
        SocialFriend friend_info;
        friend_info.friend_user_id =
            proto_friend.friend_user_id();
        friend_info.username =
            proto_friend.username();
        friend_info.nickname =
            proto_friend.nickname();
        friend_info.avatar_url =
            proto_friend.avatar_url();
        friend_info.user_status =
            proto_friend.user_status();
        friend_info.relation_status =
            proto_friend.relation_status();
        friend_info.relation_created_at =
            proto_friend.relation_created_at();
        friend_info.relation_updated_at =
            proto_friend.relation_updated_at();

        response.friends.push_back(
            std::move(friend_info)
        );
    }

    return RpcResult<ListFriendsRpcResponse>::Success(
        std::move(response)
    );
}

std::size_t SocialRpcClient::CachedTargetCountForTest() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return stub_cache_.size();
}

std::uint64_t SocialRpcClient::StubCreationCountForTest() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return stub_creation_count_;
}

std::shared_ptr<SocialRpcClient::SocialStubInterface>
SocialRpcClient::GetOrCreateStub(
    const ServiceEndpoint& endpoint
) const {
    if (endpoint.target.empty()) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(cache_mutex_);
    const std::uint64_t use_sequence = ++cache_use_sequence_;

    const auto existing = stub_cache_.find(endpoint.target);
    if (existing != stub_cache_.end()) {
        existing->second.last_used = use_sequence;
        return existing->second.stub;
    }

    auto channel = grpc::CreateChannel(
        endpoint.target,
        grpc::InsecureChannelCredentials()
    );
    if (!channel) {
        return nullptr;
    }

    auto unique_stub = tinyimx::social::v1::SocialService::NewStub(channel);
    if (!unique_stub) {
        return nullptr;
    }

    std::shared_ptr<SocialStubInterface> new_stub(
        std::move(unique_stub)
    );

    if (stub_cache_.size() >= kMaxCachedTargets) {
        const auto victim = std::min_element(
            stub_cache_.begin(),
            stub_cache_.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.second.last_used < rhs.second.last_used;
            }
        );
        if (victim != stub_cache_.end()) {
            stub_cache_.erase(victim);
        }
    }

    CachedStubEntry entry;
    entry.channel = std::move(channel);
    entry.stub = new_stub;
    entry.last_used = use_sequence;
    stub_cache_.emplace(endpoint.target, std::move(entry));
    ++stub_creation_count_;
    return new_stub;
}

RpcStatus SocialRpcClient::MapGrpcStatus(
    const grpc::Status& status
) {
    if (status.ok()) {
        return RpcStatus::Ok();
    }

    RpcErrorCode code = RpcErrorCode::kUnknown;

    switch (status.error_code()) {
        case grpc::StatusCode::INVALID_ARGUMENT:
            code = RpcErrorCode::kInvalidArgument;
            break;

        case grpc::StatusCode::CANCELLED:
            code = RpcErrorCode::kCancelled;
            break;

        case grpc::StatusCode::DEADLINE_EXCEEDED:
            code = RpcErrorCode::kDeadlineExceeded;
            break;

        case grpc::StatusCode::NOT_FOUND:
            code = RpcErrorCode::kNotFound;
            break;

        case grpc::StatusCode::ALREADY_EXISTS:
            code = RpcErrorCode::kAlreadyExists;
            break;

        case grpc::StatusCode::PERMISSION_DENIED:
            code = RpcErrorCode::kPermissionDenied;
            break;

        case grpc::StatusCode::UNAUTHENTICATED:
            code = RpcErrorCode::kUnauthenticated;
            break;

        case grpc::StatusCode::RESOURCE_EXHAUSTED:
            code = RpcErrorCode::kResourceExhausted;
            break;

        case grpc::StatusCode::FAILED_PRECONDITION:
            code = RpcErrorCode::kFailedPrecondition;
            break;

        case grpc::StatusCode::ABORTED:
            code = RpcErrorCode::kAborted;
            break;

        case grpc::StatusCode::OUT_OF_RANGE:
            code = RpcErrorCode::kOutOfRange;
            break;

        case grpc::StatusCode::UNIMPLEMENTED:
            code = RpcErrorCode::kUnimplemented;
            break;

        case grpc::StatusCode::UNAVAILABLE:
            code = RpcErrorCode::kUnavailable;
            break;

        case grpc::StatusCode::INTERNAL:
            code = RpcErrorCode::kInternal;
            break;

        case grpc::StatusCode::DATA_LOSS:
            code = RpcErrorCode::kDataLoss;
            break;

        case grpc::StatusCode::OK:
            code = RpcErrorCode::kOk;
            break;

        default:
            code = RpcErrorCode::kUnknown;
            break;
    }

    return RpcStatus{
        code,
        status.error_message()
    };
}

}  // namespace tinyimx::rpc
