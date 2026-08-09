#include "services/repository/UserRepository.h"

#include "common/logging/LogMacros.h"
#include "common/security/PasswordHasher.h"

#include <stdexcept>
#include <utility>

namespace tinyimx {
namespace {

std::uint64_t ToUInt64(
    const std::string& value
) {
    return static_cast<std::uint64_t>(
        std::stoull(value)
    );
}

std::uint32_t ToUInt32(
    const std::string& value
) {
    return static_cast<std::uint32_t>(
        std::stoul(value)
    );
}

}  // namespace

const char*
UserLookupStatusToString(
    UserLookupStatus status
) {
    switch (status) {
        case UserLookupStatus::kFound:
            return "found";

        case UserLookupStatus::kNotFound:
            return "not_found";

        case UserLookupStatus::
            kInvalidArgument:
            return "invalid_argument";

        case UserLookupStatus::
            kInvalidRecord:
            return "invalid_record";

        case UserLookupStatus::
            kStorageError:
            return "storage_error";

        default:
            return "unknown";
    }
}

const char*
UpdateLastLoginStatusToString(
    UpdateLastLoginStatus status
) {
    switch (status) {
        case UpdateLastLoginStatus::
            kSucceeded:
            return "succeeded";

        case UpdateLastLoginStatus::
            kNotFound:
            return "not_found";

        case UpdateLastLoginStatus::
            kInvalidArgument:
            return "invalid_argument";

        case UpdateLastLoginStatus::
            kInvalidRecord:
            return "invalid_record";

        case UpdateLastLoginStatus::
            kStorageError:
            return "storage_error";

        default:
            return "unknown";
    }
}

const char*
LoginVerifyStatusToString(
    LoginVerifyStatus status
) {
    switch (status) {
        case LoginVerifyStatus::kOk:
            return "ok";

        case LoginVerifyStatus::
            kInvalidArgument:
            return "invalid_argument";

        case LoginVerifyStatus::
            kUserNotFound:
            return "user_not_found";

        case LoginVerifyStatus::
            kUserDisabled:
            return "user_disabled";

        case LoginVerifyStatus::
            kPasswordNotSet:
            return "password_not_set";

        case LoginVerifyStatus::
            kWrongPassword:
            return "wrong_password";

        case LoginVerifyStatus::
            kStorageError:
            return "storage_error";

        default:
            return "unknown";
    }
}

UserRepository::UserRepository(
    MySqlConnectionPool* pool
)
    : pool_(pool) {}

UserLookupResult
UserRepository::FindById(
    std::uint64_t user_id
) {
    UserLookupResult result;

    if (user_id == 0) {
        result.status =
            UserLookupStatus::
                kInvalidArgument;

        result.message =
            "user repository find by id "
            "failed: invalid user id";

        return result;
    }

    if (pool_ == nullptr) {
        result.status =
            UserLookupStatus::
                kStorageError;

        result.message =
            "user repository find by id "
            "failed: pool is null";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            UserLookupStatus::
                kStorageError;

        result.message =
            "user repository find by id "
            "failed: acquire connection "
            "failed";

        return result;
    }

    const std::string sql =
        "SELECT "
        "user_id, "
        "username, "
        "password_salt, "
        "password_hash, "
        "nickname, "
        "avatar_url, "
        "status "
        "FROM im_users "
        "WHERE user_id = " +
        std::to_string(user_id) +
        " "
        "LIMIT 1";

    MySqlQueryResult query_result;

    if (!connection->Query(
            sql,
            &query_result
        )) {
        result.status =
            UserLookupStatus::
                kStorageError;

        result.message =
            connection->LastError();

        if (result.message.empty()) {
            result.message =
                "user repository find by id "
                "failed: query failed";
        }

        return result;
    }

    return BuildUserFromResult(
        query_result
    );
}

UserLookupResult
UserRepository::FindByUsername(
    const std::string& username
) {
    UserLookupResult result;

    if (username.empty()) {
        result.status =
            UserLookupStatus::
                kInvalidArgument;

        result.message =
            "user repository find by "
            "username failed: username "
            "is empty";

        return result;
    }

    if (pool_ == nullptr) {
        result.status =
            UserLookupStatus::
                kStorageError;

        result.message =
            "user repository find by "
            "username failed: pool is null";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            UserLookupStatus::
                kStorageError;

        result.message =
            "user repository find by "
            "username failed: acquire "
            "connection failed";

        return result;
    }

    const std::string escaped_username =
        connection->EscapeString(
            username
        );

    const std::string sql =
        "SELECT "
        "user_id, "
        "username, "
        "password_salt, "
        "password_hash, "
        "nickname, "
        "avatar_url, "
        "status "
        "FROM im_users "
        "WHERE username = '" +
        escaped_username +
        "' "
        "LIMIT 1";

    MySqlQueryResult query_result;

    if (!connection->Query(
            sql,
            &query_result
        )) {
        result.status =
            UserLookupStatus::
                kStorageError;

        result.message =
            connection->LastError();

        if (result.message.empty()) {
            result.message =
                "user repository find by "
                "username failed: query failed";
        }

        return result;
    }

    return BuildUserFromResult(
        query_result
    );
}

LoginVerifyResult
UserRepository::VerifyLogin(
    const std::string& username,
    const std::string& password
) {
    LoginVerifyResult result;

    if (username.empty() ||
        password.empty()) {
        result.status =
            LoginVerifyStatus::
                kInvalidArgument;

        result.message =
            "username or password is empty";

        return result;
    }

    const UserLookupResult lookup_result =
        FindByUsername(username);

    if (lookup_result.NotFound()) {
        result.status =
            LoginVerifyStatus::
                kUserNotFound;

        result.message =
            "user not found";

        return result;
    }

    if (!lookup_result.Found()) {
        result.status =
            LoginVerifyStatus::
                kStorageError;

        result.message =
            lookup_result.message;

        if (result.message.empty()) {
            result.message =
                "user lookup failed";
        }

        return result;
    }

    const UserRecord& user =
        lookup_result.user.value();

    if (user.status != 1) {
        result.status =
            LoginVerifyStatus::
                kUserDisabled;

        result.user = user;

        result.message =
            "user disabled";

        return result;
    }

    if (user.password_salt.empty() ||
        user.password_hash.empty()) {
        result.status =
            LoginVerifyStatus::
                kPasswordNotSet;

        result.user = user;

        result.message =
            "password not set";

        return result;
    }

    PasswordHasher hasher;

    if (!hasher.VerifyPassword(
            password,
            user.password_salt,
            user.password_hash
        )) {
        result.status =
            LoginVerifyStatus::
                kWrongPassword;

        result.user = user;

        result.message =
            "wrong password";

        return result;
    }

    const UpdateLastLoginResult
        update_result =
            UpdateLastLogin(
                user.user_id
            );

    if (!update_result.Succeeded()) {
        if (update_result.status ==
            UpdateLastLoginStatus::
                kNotFound) {
            result.status =
                LoginVerifyStatus::
                    kUserNotFound;
        } else {
            result.status =
                LoginVerifyStatus::
                    kStorageError;
        }

        result.user = user;

        result.message =
            update_result.message;

        if (result.message.empty()) {
            result.message =
                "update last login failed";
        }

        return result;
    }

    result.status =
        LoginVerifyStatus::kOk;

    result.user = user;

    result.message =
        "login accepted";

    LOG_INFO(
        "user login verified"
        << ", user_id="
        << user.user_id
        << ", username="
        << user.username
    );

    return result;
}

UpdateLastLoginResult
UserRepository::UpdateLastLogin(
    std::uint64_t user_id
) {
    UpdateLastLoginResult result;

    if (user_id == 0) {
        result.status =
            UpdateLastLoginStatus::
                kInvalidArgument;

        result.message =
            "user repository update last "
            "login failed: invalid user id";

        return result;
    }

    if (pool_ == nullptr) {
        result.status =
            UpdateLastLoginStatus::
                kStorageError;

        result.message =
            "user repository update last "
            "login failed: pool is null";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            UpdateLastLoginStatus::
                kStorageError;

        result.message =
            "user repository update last "
            "login failed: acquire "
            "connection failed";

        return result;
    }

    const std::string sql =
        "UPDATE im_users "
        "SET last_login_at = NOW() "
        "WHERE user_id = " +
        std::to_string(user_id);

    if (!connection->Execute(sql)) {
        result.status =
            UpdateLastLoginStatus::
                kStorageError;

        result.message =
            connection->LastError();

        if (result.message.empty()) {
            result.message =
                "user repository update last "
                "login failed: execute failed";
        }

        return result;
    }

    result.affected_rows =
        connection->AffectedRows();

    if (result.affected_rows > 0) {
        result.status =
            UpdateLastLoginStatus::
                kSucceeded;

        result.message =
            "last login updated";

        return result;
    }

    /*
     * MySQL的AffectedRows()==0并不一定表示用户不存在。
     *
     * 如果同一秒内重复执行：
     *
     * SET last_login_at = NOW()
     *
     * 新值与旧值相同，AffectedRows也可能为0。
     * 所以必须额外确认用户是否存在。
     */
    const std::string check_sql =
        "SELECT user_id "
        "FROM im_users "
        "WHERE user_id = " +
        std::to_string(user_id) +
        " "
        "LIMIT 1";

    MySqlQueryResult check_result;

    if (!connection->Query(
            check_sql,
            &check_result
        )) {
        result.status =
            UpdateLastLoginStatus::
                kStorageError;

        result.message =
            connection->LastError();

        if (result.message.empty()) {
            result.message =
                "user repository update last "
                "login failed: existence "
                "query failed";
        }

        return result;
    }

    if (check_result.rows.empty()) {
        result.status =
            UpdateLastLoginStatus::
                kNotFound;

        result.message =
            "user repository update last "
            "login failed: user not found";

        return result;
    }

    const auto& row =
        check_result.rows.front();

    if (row.empty()) {
        result.status =
            UpdateLastLoginStatus::
                kInvalidRecord;

        result.message =
            "user repository update last "
            "login failed: invalid "
            "existence row";

        return result;
    }

    try {
        const std::uint64_t found_user_id =
            ToUInt64(row[0]);

        if (found_user_id != user_id) {
            result.status =
                UpdateLastLoginStatus::
                    kInvalidRecord;

            result.message =
                "user repository update last "
                "login failed: unexpected "
                "user id";

            return result;
        }
    } catch (const std::exception& e) {
        result.status =
            UpdateLastLoginStatus::
                kInvalidRecord;

        result.message =
            std::string(
                "user repository update last "
                "login failed: "
            ) +
            e.what();

        return result;
    }

    result.status =
        UpdateLastLoginStatus::
            kSucceeded;

    result.affected_rows = 0;

    result.message =
        "last login already current";

    return result;
}

UserLookupResult
UserRepository::BuildUserFromResult(
    const MySqlQueryResult& result
) {
    UserLookupResult build_result;

    if (result.rows.empty()) {
        build_result.status =
            UserLookupStatus::kNotFound;

        build_result.message =
            "user not found";

        return build_result;
    }

    const auto& row =
        result.rows.front();

    if (row.size() < 7) {
        build_result.status =
            UserLookupStatus::
                kInvalidRecord;

        build_result.message =
            "user repository build user "
            "failed: invalid row size";

        return build_result;
    }

    try {
        UserRecord user;

        user.user_id =
            ToUInt64(row[0]);

        user.username = row[1];

        user.password_salt = row[2];
        user.password_hash = row[3];

        user.nickname = row[4];
        user.avatar_url = row[5];

        user.status =
            ToUInt32(row[6]);

        if (user.user_id == 0 ||
            user.username.empty()) {
            build_result.status =
                UserLookupStatus::
                    kInvalidRecord;

            build_result.message =
                "user repository build user "
                "failed: invalid record data";

            return build_result;
        }

        build_result.status =
            UserLookupStatus::kFound;

        build_result.user =
            std::move(user);

        build_result.message =
            "user found";

        return build_result;
    } catch (const std::exception& e) {
        build_result.status =
            UserLookupStatus::
                kInvalidRecord;

        build_result.message =
            std::string(
                "user repository build user "
                "failed: "
            ) +
            e.what();

        return build_result;
    }
}

}  // namespace tinyimx