#pragma once

#include "common/db/MySqlConnectionPool.h"

#include <cstdint>
#include <optional>
#include <string>

namespace tinyimx {

struct UserRecord {
    std::uint64_t user_id{0};

    std::string username;

    std::string password_salt;
    std::string password_hash;

    std::string nickname;
    std::string avatar_url;

    std::uint32_t status{0};
};

enum class UserLookupStatus {
    kFound = 0,
    kNotFound,
    kInvalidArgument,
    kInvalidRecord,
    kStorageError
};

const char*
UserLookupStatusToString(
    UserLookupStatus status
);

struct UserLookupResult {
    UserLookupStatus status{
        UserLookupStatus::kStorageError
    };

    std::optional<UserRecord> user;

    std::string message;

    bool Found() const noexcept {
        return status ==
                   UserLookupStatus::kFound &&
               user.has_value();
    }

    bool NotFound() const noexcept {
        return status ==
               UserLookupStatus::kNotFound;
    }

    bool Completed() const noexcept {
        return status ==
                   UserLookupStatus::kFound ||
               status ==
                   UserLookupStatus::kNotFound;
    }
};

enum class UpdateLastLoginStatus {
    kSucceeded = 0,
    kNotFound,
    kInvalidArgument,
    kInvalidRecord,
    kStorageError
};

const char*
UpdateLastLoginStatusToString(
    UpdateLastLoginStatus status
);

struct UpdateLastLoginResult {
    UpdateLastLoginStatus status{
        UpdateLastLoginStatus::kStorageError
    };

    std::uint64_t affected_rows{0};

    std::string message;

    bool Succeeded() const noexcept {
        return status ==
               UpdateLastLoginStatus::
                   kSucceeded;
    }
};

enum class LoginVerifyStatus {
    kOk = 0,
    kInvalidArgument,
    kUserNotFound,
    kUserDisabled,
    kPasswordNotSet,
    kWrongPassword,
    kStorageError
};

const char*
LoginVerifyStatusToString(
    LoginVerifyStatus status
);

struct LoginVerifyResult {
    LoginVerifyStatus status{
        LoginVerifyStatus::kStorageError
    };

    std::optional<UserRecord> user;

    std::string message;

    bool Success() const noexcept {
        return status ==
                   LoginVerifyStatus::kOk &&
               user.has_value();
    }
};

class UserRepository {
public:
    explicit UserRepository(
        MySqlConnectionPool* pool
    );

    UserRepository(
        const UserRepository&
    ) = delete;

    UserRepository& operator=(
        const UserRepository&
    ) = delete;

    UserLookupResult FindById(
        std::uint64_t user_id
    );

    UserLookupResult FindByUsername(
        const std::string& username
    );

    LoginVerifyResult VerifyLogin(
        const std::string& username,
        const std::string& password
    );

    UpdateLastLoginResult UpdateLastLogin(
        std::uint64_t user_id
    );

private:
    static UserLookupResult BuildUserFromResult(
        const MySqlQueryResult& result
    );

private:
    MySqlConnectionPool* pool_{nullptr};
};

}  // namespace tinyimx