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

enum class LoginVerifyStatus {
    kOk = 0,
    kInvalidArgument,
    kUserNotFound,
    kUserDisabled,
    kPasswordNotSet,
    kWrongPassword,
    kStorageError
};

const char* LoginVerifyStatusToString(LoginVerifyStatus status);

struct LoginVerifyResult {
    LoginVerifyStatus status{LoginVerifyStatus::kStorageError};
    std::optional<UserRecord> user;
    std::string message;

    bool Success() const {
        return status == LoginVerifyStatus::kOk && user.has_value();
    }
};

class UserRepository {
public:
    explicit UserRepository(MySqlConnectionPool* pool);

    UserRepository(const UserRepository&) = delete;
    UserRepository& operator=(const UserRepository&) = delete;

    std::optional<UserRecord> FindById(std::uint64_t user_id);

    std::optional<UserRecord> FindByUsername(
        const std::string& username
    );

    LoginVerifyResult VerifyLogin(const std::string& username,
                                  const std::string& password);

    bool Exists(std::uint64_t user_id);

    bool UpdateLastLogin(std::uint64_t user_id);

    const std::string& LastError() const;

private:
    std::optional<UserRecord> BuildUserFromResult(
        const MySqlQueryResult& result
    );

    void SetError(const std::string& error_message);

private:
    MySqlConnectionPool* pool_{nullptr};
    std::string last_error_;
};

}  // namespace tinyimx