#include "services/user/repository/UserRepositoryAdapter.h"

#include "services/repository/UserRepository.h"

#include <utility>

namespace tinyimx::user {

UserRepositoryAdapter::UserRepositoryAdapter(
    tinyimx::UserRepository* repository
)
    : repository_(repository) {
}

AuthenticateRepositoryResult
UserRepositoryAdapter::Authenticate(
    const std::string& username,
    const std::string& password
) {
    AuthenticateRepositoryResult output;

    if (repository_ == nullptr) {
        output.status =
            UserApplicationStatus::kStorageError;
        output.message = "user repository is unavailable";
        return output;
    }

    auto result =
        repository_->VerifyLogin(username, password);

    output.message = std::move(result.message);

    switch (result.status) {
        case tinyimx::LoginVerifyStatus::kOk:
            output.status =
                UserApplicationStatus::kSucceeded;
            output.outcome =
                AuthenticateOutcome::kAuthenticated;

            if (result.user.has_value()) {
                output.profile = ToProfile(*result.user);
            }
            return output;

        case tinyimx::LoginVerifyStatus::kUserNotFound:
            output.status =
                UserApplicationStatus::kSucceeded;
            output.outcome =
                AuthenticateOutcome::kUserNotFound;
            return output;

        case tinyimx::LoginVerifyStatus::kUserDisabled:
            output.status =
                UserApplicationStatus::kSucceeded;
            output.outcome =
                AuthenticateOutcome::kUserDisabled;
            return output;

        case tinyimx::LoginVerifyStatus::kPasswordNotSet:
            output.status =
                UserApplicationStatus::kSucceeded;
            output.outcome =
                AuthenticateOutcome::kPasswordNotSet;
            return output;

        case tinyimx::LoginVerifyStatus::kWrongPassword:
            output.status =
                UserApplicationStatus::kSucceeded;
            output.outcome =
                AuthenticateOutcome::kWrongPassword;
            return output;

        case tinyimx::LoginVerifyStatus::kInvalidArgument:
            output.status =
                UserApplicationStatus::kInvalidArgument;
            return output;

        case tinyimx::LoginVerifyStatus::kStorageError:
            output.status =
                UserApplicationStatus::kStorageError;
            return output;
    }

    output.status = UserApplicationStatus::kStorageError;
    output.message = "unknown login verification status";
    return output;
}

UserProfileRepositoryResult
UserRepositoryAdapter::GetProfile(
    std::uint64_t user_id
) {
    UserProfileRepositoryResult output;

    if (repository_ == nullptr) {
        output.status =
            UserApplicationStatus::kStorageError;
        output.message = "user repository is unavailable";
        return output;
    }

    auto result = repository_->FindById(user_id);
    output.message = std::move(result.message);

    switch (result.status) {
        case tinyimx::UserLookupStatus::kFound:
            output.status =
                UserApplicationStatus::kSucceeded;
            if (result.user.has_value()) {
                output.profile = ToProfile(*result.user);
            }
            return output;

        case tinyimx::UserLookupStatus::kNotFound:
            output.status =
                UserApplicationStatus::kNotFound;
            return output;

        case tinyimx::UserLookupStatus::kInvalidArgument:
            output.status =
                UserApplicationStatus::kInvalidArgument;
            return output;

        case tinyimx::UserLookupStatus::kInvalidRecord:
            output.status =
                UserApplicationStatus::kInvalidRecord;
            return output;

        case tinyimx::UserLookupStatus::kStorageError:
            output.status =
                UserApplicationStatus::kStorageError;
            return output;
    }

    output.status = UserApplicationStatus::kStorageError;
    output.message = "unknown user lookup status";
    return output;
}

UserProfileView UserRepositoryAdapter::ToProfile(
    const tinyimx::UserRecord& user
) {
    UserProfileView profile;
    profile.user_id = user.user_id;
    profile.username = user.username;
    profile.nickname = user.nickname;
    profile.avatar_url = user.avatar_url;
    profile.user_status = user.status;
    return profile;
}

}  // namespace tinyimx::user
