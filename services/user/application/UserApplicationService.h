#pragma once

#include "services/user/application/UserApplicationTypes.h"
#include "services/user/application/UserRepositoryPort.h"

#include <cstdint>
#include <string>

namespace tinyimx::user {

class UserApplicationService final {
public:
    explicit UserApplicationService(
        UserRepositoryPort* repository
    );

    UserApplicationService(const UserApplicationService&) = delete;
    UserApplicationService& operator=(const UserApplicationService&) = delete;

    [[nodiscard]] AuthenticateApplicationResult Authenticate(
        const std::string& username,
        const std::string& password
    );

    [[nodiscard]] GetUserProfileApplicationResult GetUserProfile(
        std::uint64_t user_id
    );

private:
    UserRepositoryPort* repository_{nullptr};  // non-owning
};

}  // namespace tinyimx::user
