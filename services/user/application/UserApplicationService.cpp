#include "services/user/application/UserApplicationService.h"

#include <utility>

namespace tinyimx::user {
namespace {

bool ValidProfile(const UserProfileView& profile) {
    return profile.user_id != 0 &&
           !profile.username.empty();
}

bool ValidOutcome(AuthenticateOutcome outcome) {
    switch (outcome) {
        case AuthenticateOutcome::kAuthenticated:
        case AuthenticateOutcome::kUserNotFound:
        case AuthenticateOutcome::kUserDisabled:
        case AuthenticateOutcome::kPasswordNotSet:
        case AuthenticateOutcome::kWrongPassword:
            return true;
    }

    return false;
}

}  // namespace

UserApplicationService::UserApplicationService(
    UserRepositoryPort* repository
)
    : repository_(repository) {
}

AuthenticateApplicationResult
UserApplicationService::Authenticate(
    const std::string& username,
    const std::string& password
) {
    AuthenticateApplicationResult output;

    if (username.empty() || password.empty()) {
        output.status =
            UserApplicationStatus::kInvalidArgument;
        output.message =
            "username or password is empty";
        return output;
    }

    if (repository_ == nullptr) {
        output.status =
            UserApplicationStatus::kStorageError;
        output.message =
            "user repository port is unavailable";
        return output;
    }

    auto repository_result =
        repository_->Authenticate(username, password);

    output.status = repository_result.status;
    output.outcome = repository_result.outcome;
    output.message = std::move(repository_result.message);

    if (!repository_result.Completed()) {
        return output;
    }

    if (!ValidOutcome(repository_result.outcome)) {
        output.status =
            UserApplicationStatus::kInvalidRecord;
        output.message =
            "user repository returned invalid authentication outcome";
        return output;
    }

    if (repository_result.outcome ==
        AuthenticateOutcome::kAuthenticated) {
        if (!repository_result.profile.has_value() ||
            !ValidProfile(*repository_result.profile)) {
            output.status =
                UserApplicationStatus::kInvalidRecord;
            output.message =
                "authenticated user profile is invalid";
            return output;
        }

        output.profile =
            std::move(repository_result.profile);
        return output;
    }

    // Authentication failures are normal business outcomes. Deliberately do
    // not expose the repository's user record/password material to callers.
    output.profile.reset();
    return output;
}

GetUserProfileApplicationResult
UserApplicationService::GetUserProfile(
    std::uint64_t user_id
) {
    GetUserProfileApplicationResult output;

    if (user_id == 0) {
        output.status =
            UserApplicationStatus::kInvalidArgument;
        output.message = "user_id must be non-zero";
        return output;
    }

    if (repository_ == nullptr) {
        output.status =
            UserApplicationStatus::kStorageError;
        output.message =
            "user repository port is unavailable";
        return output;
    }

    auto repository_result =
        repository_->GetProfile(user_id);

    output.status = repository_result.status;
    output.message = std::move(repository_result.message);

    if (repository_result.status !=
        UserApplicationStatus::kSucceeded) {
        return output;
    }

    if (!repository_result.profile.has_value() ||
        !ValidProfile(*repository_result.profile)) {
        output.status =
            UserApplicationStatus::kInvalidRecord;
        output.message = "user profile record is invalid";
        return output;
    }

    output.profile = std::move(repository_result.profile);
    return output;
}

}  // namespace tinyimx::user
