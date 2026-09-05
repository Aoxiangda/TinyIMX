#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace tinyimx::user {

enum class UserApplicationStatus {
    kSucceeded = 0,
    kInvalidArgument,
    kNotFound,
    kInvalidRecord,
    kStorageError,
};

enum class AuthenticateOutcome {
    kAuthenticated = 0,
    kUserNotFound,
    kUserDisabled,
    kPasswordNotSet,
    kWrongPassword,
};

struct UserProfileView {
    std::uint64_t user_id{0};
    std::string username;
    std::string nickname;
    std::string avatar_url;
    std::uint32_t user_status{0};
};

struct AuthenticateRepositoryResult {
    UserApplicationStatus status{
        UserApplicationStatus::kStorageError
    };
    AuthenticateOutcome outcome{
        AuthenticateOutcome::kWrongPassword
    };
    std::optional<UserProfileView> profile;
    std::string message;

    [[nodiscard]] bool Completed() const noexcept {
        return status == UserApplicationStatus::kSucceeded;
    }
};

struct UserProfileRepositoryResult {
    UserApplicationStatus status{
        UserApplicationStatus::kStorageError
    };
    std::optional<UserProfileView> profile;
    std::string message;

    [[nodiscard]] bool Found() const noexcept {
        return status == UserApplicationStatus::kSucceeded &&
               profile.has_value();
    }
};

struct AuthenticateApplicationResult {
    UserApplicationStatus status{
        UserApplicationStatus::kStorageError
    };
    AuthenticateOutcome outcome{
        AuthenticateOutcome::kWrongPassword
    };
    std::optional<UserProfileView> profile;
    std::string message;

    [[nodiscard]] bool Completed() const noexcept {
        return status == UserApplicationStatus::kSucceeded;
    }

    [[nodiscard]] bool Authenticated() const noexcept {
        return Completed() &&
               outcome == AuthenticateOutcome::kAuthenticated &&
               profile.has_value();
    }
};

struct GetUserProfileApplicationResult {
    UserApplicationStatus status{
        UserApplicationStatus::kStorageError
    };
    std::optional<UserProfileView> profile;
    std::string message;

    [[nodiscard]] bool Found() const noexcept {
        return status == UserApplicationStatus::kSucceeded &&
               profile.has_value();
    }
};

}  // namespace tinyimx::user
