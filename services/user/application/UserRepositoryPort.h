#pragma once

#include "services/user/application/UserApplicationTypes.h"

#include <cstdint>
#include <string>

namespace tinyimx::user {

class UserRepositoryPort {
public:
    virtual ~UserRepositoryPort() = default;

    UserRepositoryPort() = default;
    UserRepositoryPort(const UserRepositoryPort&) = delete;
    UserRepositoryPort& operator=(const UserRepositoryPort&) = delete;

    [[nodiscard]] virtual AuthenticateRepositoryResult Authenticate(
        const std::string& username,
        const std::string& password
    ) = 0;

    [[nodiscard]] virtual UserProfileRepositoryResult GetProfile(
        std::uint64_t user_id
    ) = 0;
};

}  // namespace tinyimx::user
