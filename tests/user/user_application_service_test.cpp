#include "services/user/application/UserApplicationService.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

class FakeUserRepositoryPort final
    : public tinyimx::user::UserRepositoryPort {
public:
    tinyimx::user::AuthenticateRepositoryResult Authenticate(
        const std::string& username,
        const std::string& password
    ) override {
        ++authenticate_calls;
        last_username = username;
        last_password = password;
        return authenticate_result;
    }

    tinyimx::user::UserProfileRepositoryResult GetProfile(
        std::uint64_t user_id
    ) override {
        ++profile_calls;
        last_user_id = user_id;
        return profile_result;
    }

    std::size_t authenticate_calls{0};
    std::size_t profile_calls{0};
    std::string last_username;
    std::string last_password;
    std::uint64_t last_user_id{0};
    tinyimx::user::AuthenticateRepositoryResult authenticate_result;
    tinyimx::user::UserProfileRepositoryResult profile_result;
};

bool Expect(bool condition, const char* name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return true;
    }

    std::cerr << "[FAIL] " << name << '\n';
    return false;
}

tinyimx::user::UserProfileView MakeProfile(
    std::uint64_t user_id = 10001
) {
    tinyimx::user::UserProfileView profile;
    profile.user_id = user_id;
    profile.username = "user" + std::to_string(user_id);
    profile.nickname = "User " + std::to_string(user_id);
    profile.avatar_url = "avatar";
    profile.user_status = 1;
    return profile;
}

bool TestValidationFastFail() {
    FakeUserRepositoryPort repository;
    tinyimx::user::UserApplicationService service(&repository);

    const auto empty_user = service.Authenticate("", "pw");
    const auto empty_password = service.Authenticate("user10001", "");
    const auto zero_profile = service.GetUserProfile(0);

    return Expect(
        empty_user.status ==
            tinyimx::user::UserApplicationStatus::kInvalidArgument &&
        empty_password.status ==
            tinyimx::user::UserApplicationStatus::kInvalidArgument &&
        zero_profile.status ==
            tinyimx::user::UserApplicationStatus::kInvalidArgument &&
        repository.authenticate_calls == 0 &&
        repository.profile_calls == 0,
        "UserApplication.ValidationFastFail"
    );
}

bool TestAuthenticateSuccess() {
    FakeUserRepositoryPort repository;
    repository.authenticate_result.status =
        tinyimx::user::UserApplicationStatus::kSucceeded;
    repository.authenticate_result.outcome =
        tinyimx::user::AuthenticateOutcome::kAuthenticated;
    repository.authenticate_result.profile = MakeProfile();
    repository.authenticate_result.message = "login accepted";

    tinyimx::user::UserApplicationService service(&repository);
    const auto result = service.Authenticate("user10001", "secret");

    return Expect(
        result.Authenticated() &&
        result.profile->user_id == 10001 &&
        repository.authenticate_calls == 1 &&
        repository.last_username == "user10001" &&
        repository.last_password == "secret",
        "UserApplication.AuthenticateSuccess"
    );
}

bool TestAuthenticateBusinessRejection() {
    FakeUserRepositoryPort repository;
    repository.authenticate_result.status =
        tinyimx::user::UserApplicationStatus::kSucceeded;
    repository.authenticate_result.outcome =
        tinyimx::user::AuthenticateOutcome::kWrongPassword;
    repository.authenticate_result.profile = MakeProfile();
    repository.authenticate_result.message = "wrong password";

    tinyimx::user::UserApplicationService service(&repository);
    const auto result = service.Authenticate("user10001", "bad");

    return Expect(
        result.Completed() &&
        !result.Authenticated() &&
        result.outcome ==
            tinyimx::user::AuthenticateOutcome::kWrongPassword &&
        !result.profile.has_value(),
        "UserApplication.AuthenticateBusinessRejection"
    );
}

bool TestStorageFailurePropagation() {
    FakeUserRepositoryPort repository;
    repository.authenticate_result.status =
        tinyimx::user::UserApplicationStatus::kStorageError;
    repository.authenticate_result.message = "mysql unavailable";

    tinyimx::user::UserApplicationService service(&repository);
    const auto result = service.Authenticate("user10001", "secret");

    return Expect(
        !result.Completed() &&
        result.status ==
            tinyimx::user::UserApplicationStatus::kStorageError &&
        result.message == "mysql unavailable",
        "UserApplication.StorageFailurePropagation"
    );
}

bool TestGetUserProfile() {
    FakeUserRepositoryPort repository;
    repository.profile_result.status =
        tinyimx::user::UserApplicationStatus::kSucceeded;
    repository.profile_result.profile = MakeProfile(10002);

    tinyimx::user::UserApplicationService service(&repository);
    const auto result = service.GetUserProfile(10002);

    return Expect(
        result.Found() &&
        result.profile->user_id == 10002 &&
        repository.profile_calls == 1 &&
        repository.last_user_id == 10002,
        "UserApplication.GetUserProfile"
    );
}

}  // namespace

int main() {
    std::cout
        << "========== TinyIMX M14-B1 User Application Tests ==========\n";

    std::size_t failed = 0;
    failed += TestValidationFastFail() ? 0 : 1;
    failed += TestAuthenticateSuccess() ? 0 : 1;
    failed += TestAuthenticateBusinessRejection() ? 0 : 1;
    failed += TestStorageFailurePropagation() ? 0 : 1;
    failed += TestGetUserProfile() ? 0 : 1;

    std::cout
        << "===========================================================\n"
        << "total = 5, failed = " << failed << '\n';

    return failed == 0 ? 0 : 1;
}
