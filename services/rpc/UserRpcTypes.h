#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace tinyimx::rpc {

enum class AuthenticateRpcOutcome {
    kAuthenticated = 0,
    kUserNotFound,
    kUserDisabled,
    kPasswordNotSet,
    kWrongPassword,
};

struct UserProfileRpcView {
    std::uint64_t user_id{0};
    std::string username;
    std::string nickname;
    std::string avatar_url;
    std::uint32_t user_status{0};
};

struct AuthenticateRpcRequest {
    std::string username;
    std::string password;
};

struct AuthenticateRpcResponse {
    AuthenticateRpcOutcome outcome{
        AuthenticateRpcOutcome::kWrongPassword
    };
    std::optional<UserProfileRpcView> profile;
    std::string message;

    [[nodiscard]] bool authenticated() const noexcept {
        return outcome == AuthenticateRpcOutcome::kAuthenticated &&
               profile.has_value();
    }
};

struct GetUserProfileRpcRequest {
    std::uint64_t user_id{0};
};

struct GetUserProfileRpcResponse {
    UserProfileRpcView profile;
};

}  // namespace tinyimx::rpc
