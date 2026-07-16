#include "services/repository/UserRepository.h"

#include "common/logging/LogMacros.h"
#include "common/security/PasswordHasher.h"

#include <stdexcept>

namespace tinyimx {
namespace {

std::uint64_t ToUInt64(const std::string& value) {
    return static_cast<std::uint64_t>(std::stoull(value));
}

std::uint32_t ToUInt32(const std::string& value) {
    return static_cast<std::uint32_t>(std::stoul(value));
}

}  // namespace

const char* LoginVerifyStatusToString(LoginVerifyStatus status) {
    switch (status) {
        case LoginVerifyStatus::kOk:
            return "ok";
        case LoginVerifyStatus::kInvalidArgument:
            return "invalid_argument";
        case LoginVerifyStatus::kUserNotFound:
            return "user_not_found";
        case LoginVerifyStatus::kUserDisabled:
            return "user_disabled";
        case LoginVerifyStatus::kPasswordNotSet:
            return "password_not_set";
        case LoginVerifyStatus::kWrongPassword:
            return "wrong_password";
        case LoginVerifyStatus::kStorageError:
            return "storage_error";
        default:
            return "unknown";
    }
}

UserRepository::UserRepository(MySqlConnectionPool* pool)
    : pool_(pool) {}

std::optional<UserRecord> UserRepository::FindById(
    std::uint64_t user_id
) {
    if (pool_ == nullptr) {
        SetError("user repository find by id failed: pool is null");
        return std::nullopt;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError("user repository find by id failed: acquire connection failed");
        return std::nullopt;
    }

    const std::string sql =
        "SELECT user_id, username, password_salt, password_hash, "
        "nickname, avatar_url, status "
        "FROM im_users "
        "WHERE user_id = " + std::to_string(user_id) + " "
        "LIMIT 1";

    MySqlQueryResult result;
    if (!connection->Query(sql, &result)) {
        SetError(connection->LastError());
        return std::nullopt;
    }

    return BuildUserFromResult(result);
}

std::optional<UserRecord> UserRepository::FindByUsername(
    const std::string& username
) {
    if (pool_ == nullptr) {
        SetError("user repository find by username failed: pool is null");
        return std::nullopt;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError("user repository find by username failed: acquire connection failed");
        return std::nullopt;
    }

    const std::string escaped_username =
        connection->EscapeString(username);

    const std::string sql =
        "SELECT user_id, username, password_salt, password_hash, "
        "nickname, avatar_url, status "
        "FROM im_users "
        "WHERE username = '" + escaped_username + "' "
        "LIMIT 1";

    MySqlQueryResult result;
    if (!connection->Query(sql, &result)) {
        SetError(connection->LastError());
        return std::nullopt;
    }

    return BuildUserFromResult(result);
}

LoginVerifyResult UserRepository::VerifyLogin(
    const std::string& username,
    const std::string& password
) {
    LoginVerifyResult result;

    if (username.empty() || password.empty()) {
        result.status = LoginVerifyStatus::kInvalidArgument;
        result.message = "username or password is empty";
        return result;
    }

    const auto user = FindByUsername(username);

    if (!user.has_value()) {
        result.status = LoginVerifyStatus::kUserNotFound;
        result.message = "user not found";
        return result;
    }

    if (user->status != 1) {
        result.status = LoginVerifyStatus::kUserDisabled;
        result.user = user;
        result.message = "user disabled";
        return result;
    }

    if (user->password_salt.empty() ||
        user->password_hash.empty()) {
        result.status = LoginVerifyStatus::kPasswordNotSet;
        result.user = user;
        result.message = "password not set";
        return result;
    }

    PasswordHasher hasher;

    if (!hasher.VerifyPassword(
            password,
            user->password_salt,
            user->password_hash)) {
        result.status = LoginVerifyStatus::kWrongPassword;
        result.user = user;
        result.message = "wrong password";
        return result;
    }

    if (!UpdateLastLogin(user->user_id)) {
        result.status = LoginVerifyStatus::kStorageError;
        result.user = user;
        result.message = "update last login failed";
        return result;
    }

    result.status = LoginVerifyStatus::kOk;
    result.user = user;
    result.message = "login accepted";

    LOG_INFO("user login verified"
             << ", user_id=" << user->user_id
             << ", username=" << user->username);

    return result;
}

bool UserRepository::Exists(std::uint64_t user_id) {
    return FindById(user_id).has_value();
}

/*
    bool UserRepository::UpdateLastLogin(std::uint64_t user_id) {
        if (pool_ == nullptr) {
            SetError("user repository update last login failed: pool is null");
            return false;
        }

        auto connection = pool_->Acquire();
        if (!connection) {
            SetError("user repository update last login failed: acquire connection failed");
            return false;
        }

        const std::string sql =
            "UPDATE im_users "
            "SET last_login_at = NOW() "
            "WHERE user_id = " + std::to_string(user_id);

        if (!connection->Execute(sql)) {
            SetError(connection->LastError());
            return false;
        }

        if (connection->AffectedRows() == 0) {
            SetError("user repository update last login failed: user not found");
            return false;
        }

        last_error_.clear();
        return true;
    }
*/

bool UserRepository::UpdateLastLogin(std::uint64_t user_id) {
    if (pool_ == nullptr) {
        SetError("user repository update last login failed: pool is null");
        return false;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError("user repository update last login failed: acquire connection failed");
        return false;
    }

    const std::string sql =
        "UPDATE im_users "
        "SET last_login_at = NOW() "
        "WHERE user_id = " + std::to_string(user_id);

    if (!connection->Execute(sql)) {
        SetError(connection->LastError());
        return false;
    }

    if (connection->AffectedRows() == 0) {
        const std::string check_sql =
            "SELECT user_id "
            "FROM im_users "
            "WHERE user_id = " + std::to_string(user_id) + " "
            "LIMIT 1";

        MySqlQueryResult check_result;

        if (!connection->Query(check_sql, &check_result)) {
            SetError(connection->LastError());
            return false;
        }

        if (check_result.rows.empty()) {
            SetError("user repository update last login failed: user not found");
            return false;
        }
    }

    last_error_.clear();
    return true;
}

const std::string& UserRepository::LastError() const {
    return last_error_;
}

std::optional<UserRecord> UserRepository::BuildUserFromResult(
    const MySqlQueryResult& result
) {
    if (result.rows.empty()) {
        last_error_.clear();
        return std::nullopt;
    }

    const auto& row = result.rows[0];

    if (row.size() < 7) {
        SetError("user repository build user failed: invalid row size");
        return std::nullopt;
    }

    try {
        UserRecord user;
        user.user_id = ToUInt64(row[0]);
        user.username = row[1];
        user.password_salt = row[2];
        user.password_hash = row[3];
        user.nickname = row[4];
        user.avatar_url = row[5];
        user.status = ToUInt32(row[6]);

        last_error_.clear();
        return user;
    } catch (const std::exception& e) {
        SetError(
            std::string("user repository build user failed: ") +
            e.what()
        );
        return std::nullopt;
    }
}

void UserRepository::SetError(const std::string& error_message) {
    last_error_ = error_message;
    LOG_ERROR(error_message);
}

}  // namespace tinyimx