#include "services/rpc/UserRpcClient.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace tinyimx::rpc {
namespace {

template <typename T>
RpcResult<T> Failure(
    RpcErrorCode code,
    std::string message
) {
    return RpcResult<T>::Failure(
        code,
        std::move(message)
    );
}

UserProfileRpcView ToRpcProfile(
    const tinyimx::user::v1::UserProfile& profile
) {
    UserProfileRpcView output;
    output.user_id = profile.user_id();
    output.username = profile.username();
    output.nickname = profile.nickname();
    output.avatar_url = profile.avatar_url();
    output.user_status = profile.user_status();
    return output;
}

bool ValidProfile(const UserProfileRpcView& profile) {
    return profile.user_id != 0 &&
           !profile.username.empty();
}

std::optional<AuthenticateRpcOutcome> MapOutcome(
    tinyimx::user::v1::AuthenticateResult result
) {
    switch (result) {
        case tinyimx::user::v1::AUTHENTICATE_RESULT_AUTHENTICATED:
            return AuthenticateRpcOutcome::kAuthenticated;
        case tinyimx::user::v1::AUTHENTICATE_RESULT_USER_NOT_FOUND:
            return AuthenticateRpcOutcome::kUserNotFound;
        case tinyimx::user::v1::AUTHENTICATE_RESULT_USER_DISABLED:
            return AuthenticateRpcOutcome::kUserDisabled;
        case tinyimx::user::v1::AUTHENTICATE_RESULT_PASSWORD_NOT_SET:
            return AuthenticateRpcOutcome::kPasswordNotSet;
        case tinyimx::user::v1::AUTHENTICATE_RESULT_WRONG_PASSWORD:
            return AuthenticateRpcOutcome::kWrongPassword;
        case tinyimx::user::v1::AUTHENTICATE_RESULT_UNSPECIFIED:
            return std::nullopt;
    }

    return std::nullopt;
}

void FillMeta(
    const RpcCallOptions& options,
    tinyimx::common::v1::RequestMeta* meta
) {
    if (meta == nullptr) {
        return;
    }

    meta->set_request_id(options.request_id);
    meta->set_trace_id(options.trace_id);
    meta->set_caller_service(options.caller_service);
    meta->set_caller_instance(options.caller_instance);
}

}  // namespace

UserRpcClient::UserRpcClient(
    std::shared_ptr<const ServiceEndpointProvider>
        endpoint_provider
)
    : endpoint_provider_(std::move(endpoint_provider)) {
}

RpcResult<AuthenticateRpcResponse>
UserRpcClient::Authenticate(
    const AuthenticateRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.username.empty() ||
        request.password.empty()) {
        return Failure<AuthenticateRpcResponse>(
            RpcErrorCode::kInvalidArgument,
            "Authenticate username/password must be non-empty"
        );
    }

    if (options.remaining_timeout <=
        std::chrono::milliseconds::zero()) {
        return Failure<AuthenticateRpcResponse>(
            RpcErrorCode::kDeadlineExceeded,
            "Authenticate remaining RPC budget is exhausted"
        );
    }

    if (!endpoint_provider_) {
        return Failure<AuthenticateRpcResponse>(
            RpcErrorCode::kUnavailable,
            "UserService endpoint provider is not configured"
        );
    }

    const auto endpoint =
        endpoint_provider_->Resolve(ServiceKind::kUser);

    if (!endpoint.has_value() ||
        endpoint->target.empty()) {
        return Failure<AuthenticateRpcResponse>(
            RpcErrorCode::kUnavailable,
            "UserService endpoint is unavailable"
        );
    }

    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return Failure<AuthenticateRpcResponse>(
            RpcErrorCode::kUnavailable,
            "UserService gRPC stub could not be created"
        );
    }

    tinyimx::user::v1::AuthenticateRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_username(request.username);
    proto_request.set_password(request.password);

    tinyimx::user::v1::AuthenticateResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(
        std::chrono::system_clock::now() +
        options.remaining_timeout
    );

    const grpc::Status grpc_status =
        stub->Authenticate(
            &context,
            proto_request,
            &proto_response
        );

    if (!grpc_status.ok()) {
        const RpcStatus mapped = MapGrpcStatus(grpc_status);
        return Failure<AuthenticateRpcResponse>(
            mapped.code,
            mapped.message
        );
    }

    const auto outcome = MapOutcome(proto_response.result());
    if (!outcome.has_value()) {
        return Failure<AuthenticateRpcResponse>(
            RpcErrorCode::kDataLoss,
            "UserService returned unspecified authentication outcome"
        );
    }

    AuthenticateRpcResponse output;
    output.outcome = *outcome;
    output.message = proto_response.message();

    if (output.outcome ==
        AuthenticateRpcOutcome::kAuthenticated) {
        if (!proto_response.has_profile()) {
            return Failure<AuthenticateRpcResponse>(
                RpcErrorCode::kDataLoss,
                "authenticated UserService response has no profile"
            );
        }

        auto profile = ToRpcProfile(proto_response.profile());
        if (!ValidProfile(profile)) {
            return Failure<AuthenticateRpcResponse>(
                RpcErrorCode::kDataLoss,
                "authenticated UserService profile is invalid"
            );
        }

        output.profile = std::move(profile);
    }

    return RpcResult<AuthenticateRpcResponse>::Success(
        std::move(output)
    );
}

RpcResult<GetUserProfileRpcResponse>
UserRpcClient::GetUserProfile(
    const GetUserProfileRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.user_id == 0) {
        return Failure<GetUserProfileRpcResponse>(
            RpcErrorCode::kInvalidArgument,
            "GetUserProfile user_id must be non-zero"
        );
    }

    if (options.remaining_timeout <=
        std::chrono::milliseconds::zero()) {
        return Failure<GetUserProfileRpcResponse>(
            RpcErrorCode::kDeadlineExceeded,
            "GetUserProfile remaining RPC budget is exhausted"
        );
    }

    if (!endpoint_provider_) {
        return Failure<GetUserProfileRpcResponse>(
            RpcErrorCode::kUnavailable,
            "UserService endpoint provider is not configured"
        );
    }

    const auto endpoint =
        endpoint_provider_->Resolve(ServiceKind::kUser);

    if (!endpoint.has_value() ||
        endpoint->target.empty()) {
        return Failure<GetUserProfileRpcResponse>(
            RpcErrorCode::kUnavailable,
            "UserService endpoint is unavailable"
        );
    }

    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return Failure<GetUserProfileRpcResponse>(
            RpcErrorCode::kUnavailable,
            "UserService gRPC stub could not be created"
        );
    }

    tinyimx::user::v1::GetUserProfileRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_user_id(request.user_id);

    tinyimx::user::v1::GetUserProfileResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(
        std::chrono::system_clock::now() +
        options.remaining_timeout
    );

    const grpc::Status grpc_status =
        stub->GetUserProfile(
            &context,
            proto_request,
            &proto_response
        );

    if (!grpc_status.ok()) {
        const RpcStatus mapped = MapGrpcStatus(grpc_status);
        return Failure<GetUserProfileRpcResponse>(
            mapped.code,
            mapped.message
        );
    }

    if (!proto_response.has_profile()) {
        return Failure<GetUserProfileRpcResponse>(
            RpcErrorCode::kDataLoss,
            "UserService profile response is missing profile"
        );
    }

    GetUserProfileRpcResponse output;
    output.profile = ToRpcProfile(proto_response.profile());

    if (!ValidProfile(output.profile)) {
        return Failure<GetUserProfileRpcResponse>(
            RpcErrorCode::kDataLoss,
            "UserService profile response is invalid"
        );
    }

    return RpcResult<GetUserProfileRpcResponse>::Success(
        std::move(output)
    );
}

std::shared_ptr<UserRpcClient::UserStubInterface>
UserRpcClient::GetOrCreateStub(
    const ServiceEndpoint& endpoint
) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    if (cached_stub_ &&
        cached_target_ == endpoint.target) {
        return cached_stub_;
    }

    auto channel = grpc::CreateChannel(
        endpoint.target,
        grpc::InsecureChannelCredentials()
    );

    if (!channel) {
        return nullptr;
    }

    auto unique_stub =
        tinyimx::user::v1::UserService::NewStub(channel);

    if (!unique_stub) {
        return nullptr;
    }

    std::shared_ptr<UserStubInterface> new_stub(
        std::move(unique_stub)
    );

    cached_target_ = endpoint.target;
    cached_channel_ = std::move(channel);
    cached_stub_ = new_stub;
    return new_stub;
}

RpcStatus UserRpcClient::MapGrpcStatus(
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

    return RpcStatus{code, status.error_message()};
}

}  // namespace tinyimx::rpc
