#include "services/user/service/UserServiceImpl.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdlib>
#include <thread>

namespace tinyimx::user {
namespace {

grpc::Status MapApplicationFailure(
    UserApplicationStatus status,
    const std::string& message
) {
    switch (status) {
        case UserApplicationStatus::kInvalidArgument:
            return grpc::Status(
                grpc::StatusCode::INVALID_ARGUMENT,
                message
            );

        case UserApplicationStatus::kNotFound:
            return grpc::Status(
                grpc::StatusCode::NOT_FOUND,
                message
            );

        case UserApplicationStatus::kInvalidRecord:
            return grpc::Status(
                grpc::StatusCode::DATA_LOSS,
                message
            );

        case UserApplicationStatus::kStorageError:
            return grpc::Status(
                grpc::StatusCode::UNAVAILABLE,
                message
            );

        case UserApplicationStatus::kSucceeded:
            return grpc::Status::OK;
    }

    return grpc::Status(
        grpc::StatusCode::INTERNAL,
        "unknown UserService application status"
    );
}

tinyimx::user::v1::AuthenticateResult ToProtoOutcome(
    AuthenticateOutcome outcome
) {
    switch (outcome) {
        case AuthenticateOutcome::kAuthenticated:
            return tinyimx::user::v1::
                AUTHENTICATE_RESULT_AUTHENTICATED;
        case AuthenticateOutcome::kUserNotFound:
            return tinyimx::user::v1::
                AUTHENTICATE_RESULT_USER_NOT_FOUND;
        case AuthenticateOutcome::kUserDisabled:
            return tinyimx::user::v1::
                AUTHENTICATE_RESULT_USER_DISABLED;
        case AuthenticateOutcome::kPasswordNotSet:
            return tinyimx::user::v1::
                AUTHENTICATE_RESULT_PASSWORD_NOT_SET;
        case AuthenticateOutcome::kWrongPassword:
            return tinyimx::user::v1::
                AUTHENTICATE_RESULT_WRONG_PASSWORD;
    }

    return tinyimx::user::v1::
        AUTHENTICATE_RESULT_UNSPECIFIED;
}

std::chrono::milliseconds AuthenticateFaultDelay() noexcept {
    const char* raw = std::getenv(
        "TINYIMX_FAULT_USER_AUTH_DELAY_MS"
    );
    if (raw == nullptr || raw[0] == '\0') {
        return std::chrono::milliseconds{0};
    }

    char* end = nullptr;
    const long value = std::strtol(raw, &end, 10);
    if (end == raw || *end != '\0' || value <= 0) {
        return std::chrono::milliseconds{0};
    }

    constexpr long kMaxFaultDelayMs = 60000;
    return std::chrono::milliseconds{
        value > kMaxFaultDelayMs ? kMaxFaultDelayMs : value
    };
}


std::chrono::milliseconds ProfileFaultDelay() noexcept {
    const char* raw = std::getenv(
        "TINYIMX_FAULT_USER_PROFILE_DELAY_MS"
    );
    if (raw == nullptr || raw[0] == '\0') {
        return std::chrono::milliseconds{0};
    }

    char* end = nullptr;
    const long value = std::strtol(raw, &end, 10);
    if (end == raw || *end != '\0' || value <= 0) {
        return std::chrono::milliseconds{0};
    }

    constexpr long kMaxFaultDelayMs = 60000;
    return std::chrono::milliseconds{
        value > kMaxFaultDelayMs ? kMaxFaultDelayMs : value
    };
}

void FillProtoProfile(
    const UserProfileView& profile,
    tinyimx::user::v1::UserProfile* output
) {
    if (output == nullptr) {
        return;
    }

    output->set_user_id(profile.user_id);
    output->set_username(profile.username);
    output->set_nickname(profile.nickname);
    output->set_avatar_url(profile.avatar_url);
    output->set_user_status(profile.user_status);
}

}  // namespace

UserServiceImpl::UserServiceImpl(
    UserApplicationService* application_service
)
    : application_service_(application_service) {
}

grpc::Status UserServiceImpl::Authenticate(
    grpc::ServerContext* context,
    const tinyimx::user::v1::AuthenticateRequest* request,
    tinyimx::user::v1::AuthenticateResponse* response
) {
    if (context == nullptr ||
        request == nullptr ||
        response == nullptr) {
        return grpc::Status(
            grpc::StatusCode::INVALID_ARGUMENT,
            "invalid Authenticate RPC arguments"
        );
    }

    if (application_service_ == nullptr) {
        return grpc::Status(
            grpc::StatusCode::UNAVAILABLE,
            "UserService application service is unavailable"
        );
    }

    const auto fault_delay = AuthenticateFaultDelay();
    if (fault_delay > std::chrono::milliseconds::zero()) {
        std::this_thread::sleep_for(fault_delay);
        if (context->IsCancelled()) {
            return grpc::Status(
                grpc::StatusCode::CANCELLED,
                "Authenticate cancelled during injected fault delay"
            );
        }
    }

    auto result =
        application_service_->Authenticate(
            request->username(),
            request->password()
        );

    if (!result.Completed()) {
        return MapApplicationFailure(
            result.status,
            result.message
        );
    }

    response->set_result(
        ToProtoOutcome(result.outcome)
    );
    response->set_message(result.message);

    if (result.Authenticated()) {
        FillProtoProfile(
            *result.profile,
            response->mutable_profile()
        );
    }

    return grpc::Status::OK;
}

grpc::Status UserServiceImpl::GetUserProfile(
    grpc::ServerContext* context,
    const tinyimx::user::v1::GetUserProfileRequest* request,
    tinyimx::user::v1::GetUserProfileResponse* response
) {
    if (context == nullptr ||
        request == nullptr ||
        response == nullptr) {
        return grpc::Status(
            grpc::StatusCode::INVALID_ARGUMENT,
            "invalid GetUserProfile RPC arguments"
        );
    }

    if (application_service_ == nullptr) {
        return grpc::Status(
            grpc::StatusCode::UNAVAILABLE,
            "UserService application service is unavailable"
        );
    }

    const auto fault_delay = ProfileFaultDelay();
    if (fault_delay > std::chrono::milliseconds::zero()) {
        std::this_thread::sleep_for(fault_delay);
        if (context->IsCancelled()) {
            return grpc::Status(
                grpc::StatusCode::CANCELLED,
                "GetUserProfile cancelled during injected fault delay"
            );
        }
    }

    auto result =
        application_service_->GetUserProfile(
            request->user_id()
        );

    if (!result.Found()) {
        return MapApplicationFailure(
            result.status,
            result.message
        );
    }

    FillProtoProfile(
        *result.profile,
        response->mutable_profile()
    );

    return grpc::Status::OK;
}

}  // namespace tinyimx::user
