#pragma once

#include "services/user/application/UserRepositoryPort.h"

namespace tinyimx {
class UserRepository;
struct UserRecord;
}

namespace tinyimx::user {

class UserRepositoryAdapter final
    : public UserRepositoryPort {
public:
    explicit UserRepositoryAdapter(
        tinyimx::UserRepository* repository
    );

    [[nodiscard]] AuthenticateRepositoryResult Authenticate(
        const std::string& username,
        const std::string& password
    ) override;

    [[nodiscard]] UserProfileRepositoryResult GetProfile(
        std::uint64_t user_id
    ) override;

private:
    [[nodiscard]] static UserProfileView ToProfile(
        const tinyimx::UserRecord& user
    );

private:
    tinyimx::UserRepository* repository_{nullptr};  // non-owning
};

}  // namespace tinyimx::user
