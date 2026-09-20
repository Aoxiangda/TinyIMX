#include "common/db/MySqlConnection.h"

#include "common/logging/LogMacros.h"

#include <cstdlib>
#include <memory>

namespace tinyimx {

MySqlConnection::MySqlConnection() {
    mysql_ = ::mysql_init(nullptr);

    if (mysql_ == nullptr) {
        SetError("mysql_init failed");
    }
}

MySqlConnection::~MySqlConnection() {
    Close();
}

bool MySqlConnection::Connect(const MySqlConfig& config) {
    if (mysql_ == nullptr) {
        SetError("mysql handle is null");
        return false;
    }

    Close();

    mysql_ = ::mysql_init(nullptr);
    if (mysql_ == nullptr) {
        SetError("mysql_init failed");
        return false;
    }

    const unsigned int connect_timeout = 5;
    ::mysql_options(
        mysql_,
        MYSQL_OPT_CONNECT_TIMEOUT,
        &connect_timeout
    );

    const unsigned int read_timeout = 5;
    ::mysql_options(
        mysql_,
        MYSQL_OPT_READ_TIMEOUT,
        &read_timeout
    );

    const unsigned int write_timeout = 5;
    ::mysql_options(
        mysql_,
        MYSQL_OPT_WRITE_TIMEOUT,
        &write_timeout
    );

    MYSQL* result = ::mysql_real_connect(
        mysql_,
        config.host.c_str(),
        config.user.c_str(),
        config.password.c_str(),
        config.database.c_str(),
        static_cast<unsigned int>(config.port),
        nullptr,
        CLIENT_MULTI_STATEMENTS
    );

    if (result == nullptr) {
        SetMysqlError("mysql_real_connect failed");
        Close();
        return false;
    }

    if (::mysql_set_character_set(mysql_, "utf8mb4") != 0) {
        SetMysqlError("mysql_set_character_set utf8mb4 failed");
        Close();
        return false;
    }

    //connected_ = true;
    //last_error_.clear();

    connected_ = true;
    transaction_active_ = false;

    last_insert_id_ = 0;
    affected_rows_ = 0;

    last_error_.clear();

    LOG_INFO("mysql connected"
             << ", host=" << config.host
             << ", port=" << config.port
             << ", database=" << config.database
             << ", user=" << config.user);

    return true;
}

void MySqlConnection::Close() {
    if (mysql_ != nullptr) {
        if (transaction_active_) {
            if (::mysql_rollback(mysql_) != 0) {
                LOG_WARN(
                    "mysql rollback before close failed"
                    << ", errno=" << ::mysql_errno(mysql_)
                    << ", error=" << ::mysql_error(mysql_)
                );
            }
        }

        ::mysql_close(mysql_);
        mysql_ = nullptr;
    }

    connected_ = false;
    transaction_active_ = false;

    last_insert_id_ = 0;
    affected_rows_ = 0;
}

bool MySqlConnection::IsConnected() const {
    return connected_ && mysql_ != nullptr;
}

bool MySqlConnection::Ping() {
    if (!IsConnected()) {
        SetError("mysql ping failed: not connected");
        return false;
    }

    if (::mysql_ping(mysql_) != 0) {
        SetMysqlError("mysql_ping failed");
        connected_ = false;
        return false;
    }

    return true;
}
/*

bool MySqlConnection::Execute(const std::string& sql) {
    if (!IsConnected()) {
        SetError("mysql execute failed: not connected");
        return false;
    }

    if (::mysql_query(mysql_, sql.c_str()) != 0) {
        SetMysqlError("mysql_query execute failed");
        return false;
    }

    MYSQL_RES* result = nullptr;

    do {
        result = ::mysql_store_result(mysql_);
        if (result != nullptr) {
            ::mysql_free_result(result);
        }
    } while (::mysql_next_result(mysql_) == 0);

    last_error_.clear();
    return true;
}

*/
bool MySqlConnection::Execute(const std::string& sql) {
    if (!IsConnected()) {
        SetError("mysql execute failed: not connected");
        return false;
    }

    if (::mysql_query(mysql_, sql.c_str()) != 0) {
        SetMysqlError("mysql_query execute failed");
        return false;
    }

    const my_ulonglong affected =
        ::mysql_affected_rows(mysql_);

    if (affected == static_cast<my_ulonglong>(-1)) {
        affected_rows_ = 0;
    } else {
        affected_rows_ = static_cast<std::uint64_t>(affected);
    }

    last_insert_id_ =
        static_cast<std::uint64_t>(::mysql_insert_id(mysql_));

    while (true) {
        MYSQL_RES* result = ::mysql_store_result(mysql_);

        if (result != nullptr) {
            ::mysql_free_result(result);
        } else {
            if (::mysql_field_count(mysql_) != 0) {
                SetMysqlError("mysql_store_result failed");
                return false;
            }
        }

        const int next_status = ::mysql_next_result(mysql_);

        if (next_status == -1) {
            break;
        }

        if (next_status > 0) {
            SetMysqlError("mysql_next_result failed");
            return false;
        }
    }

    last_error_.clear();
    return true;
}

bool MySqlConnection::Query(
    const std::string& sql,
    MySqlQueryResult* result
) {
    if (result == nullptr) {
        SetError("mysql query failed: result is null");
        return false;
    }

    result->fields.clear();
    result->rows.clear();

    if (!IsConnected()) {
        SetError("mysql query failed: not connected");
        return false;
    }

    if (::mysql_query(mysql_, sql.c_str()) != 0) {
        SetMysqlError("mysql_query query failed");
        return false;
    }

    MYSQL_RES* mysql_result = ::mysql_store_result(mysql_);
    if (mysql_result == nullptr) {
        if (::mysql_field_count(mysql_) == 0) {
            last_error_.clear();
            return true;
        }

        SetMysqlError("mysql_store_result failed");
        return false;
    }

    std::unique_ptr<MYSQL_RES, decltype(&::mysql_free_result)>
        result_guard(mysql_result, ::mysql_free_result);

    const unsigned int field_count =
        ::mysql_num_fields(mysql_result);

    MYSQL_FIELD* fields = ::mysql_fetch_fields(mysql_result);

    for (unsigned int i = 0; i < field_count; ++i) {
        result->fields.emplace_back(fields[i].name);
    }

    MYSQL_ROW row = nullptr;

    while ((row = ::mysql_fetch_row(mysql_result)) != nullptr) {
        unsigned long* lengths =
            ::mysql_fetch_lengths(mysql_result);

        std::vector<std::string> row_values;
        row_values.reserve(field_count);

        for (unsigned int i = 0; i < field_count; ++i) {
            if (row[i] == nullptr) {
                row_values.emplace_back("");
                continue;
            }

            row_values.emplace_back(
                row[i],
                static_cast<std::size_t>(lengths[i])
            );
        }

        result->rows.push_back(std::move(row_values));
    }

    last_error_.clear();
    return true;
}

/*
    std::uint64_t MySqlConnection::LastInsertId() const {
        if (mysql_ == nullptr) {
            return 0;
        }

        return static_cast<std::uint64_t>(
            ::mysql_insert_id(mysql_)
        );
    }
*/


bool MySqlConnection::BeginTransaction() {
    if (!IsConnected()) {
        SetError(
            "mysql begin transaction failed: not connected"
        );
        return false;
    }

    if (transaction_active_) {
        SetError(
            "mysql begin transaction failed: "
            "transaction already active"
        );
        return false;
    }

    if (::mysql_query(mysql_, "START TRANSACTION") != 0) {
        SetMysqlError(
            "mysql start transaction failed"
        );
        return false;
    }

    transaction_active_ = true;

    last_insert_id_ = 0;
    affected_rows_ = 0;

    last_error_.clear();
    return true;
}

bool MySqlConnection::BeginConsistentReadTransaction() {
    if (!IsConnected()) {
        SetError(
            "mysql begin consistent read transaction failed: not connected"
        );
        return false;
    }

    if (transaction_active_) {
        SetError(
            "mysql begin consistent read transaction failed: "
            "transaction already active"
        );
        return false;
    }

    // Do not depend on the server/session default isolation level. B2 group
    // fanout correctness requires the permission reads and recipient list to
    // come from one point-in-time snapshot even if an operator changes the
    // global MySQL isolation setting. SET TRANSACTION affects only the next
    // transaction on this connection.
    if (::mysql_query(mysql_, "SET TRANSACTION ISOLATION LEVEL REPEATABLE READ") != 0) {
        SetMysqlError(
            "mysql set consistent read isolation failed"
        );
        return false;
    }

    if (::mysql_query(mysql_,
                      "START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY") != 0) {
        SetMysqlError(
            "mysql start consistent read transaction failed"
        );
        return false;
    }

    transaction_active_ = true;
    last_insert_id_ = 0;
    affected_rows_ = 0;
    last_error_.clear();
    return true;
}

std::uint64_t MySqlConnection::LastInsertId() const {
    return last_insert_id_;
}

/*
    std::uint64_t MySqlConnection::AffectedRows() const {
    if (mysql_ == nullptr) {
        return 0;
    }

    return static_cast<std::uint64_t>(
        ::mysql_affected_rows(mysql_)
    );
}

*/

bool MySqlConnection::Commit() {
    if (!IsConnected()) {
        SetError(
            "mysql commit failed: not connected"
        );
        return false;
    }

    if (!transaction_active_) {
        SetError(
            "mysql commit failed: no active transaction"
        );
        return false;
    }

    if (::mysql_commit(mysql_) != 0) {
        SetMysqlError("mysql commit failed");
        return false;
    }

    transaction_active_ = false;

    last_insert_id_ = 0;
    affected_rows_ = 0;

    last_error_.clear();
    return true;
}

bool MySqlConnection::Rollback() {
    if (!IsConnected()) {
        SetError(
            "mysql rollback failed: not connected"
        );
        return false;
    }

    if (!transaction_active_) {
        SetError(
            "mysql rollback failed: no active transaction"
        );
        return false;
    }

    if (::mysql_rollback(mysql_) != 0) {
        SetMysqlError("mysql rollback failed");
        return false;
    }

    transaction_active_ = false;

    last_insert_id_ = 0;
    affected_rows_ = 0;

    last_error_.clear();
    return true;
}

bool MySqlConnection::InTransaction() const {
    return transaction_active_;
}

std::uint64_t MySqlConnection::AffectedRows() const {
    return affected_rows_;
}

std::string MySqlConnection::EscapeString(
    const std::string& value
) {
    if (!IsConnected()) {
        return "";
    }

    std::string escaped;
    escaped.resize(value.size() * 2 + 1);

    const unsigned long length = ::mysql_real_escape_string(
        mysql_,
        escaped.data(),
        value.data(),
        static_cast<unsigned long>(value.size())
    );

    escaped.resize(static_cast<std::size_t>(length));
    return escaped;
}

const std::string& MySqlConnection::LastError() const {
    return last_error_;
}

void MySqlConnection::SetError(
    const std::string& error_message
) {
    last_error_ = error_message;
    LOG_ERROR(error_message);
}

void MySqlConnection::SetMysqlError(
    const std::string& prefix
) {
    if (mysql_ == nullptr) {
        SetError(prefix + ": mysql handle is null");
        return;
    }

    last_error_ =
        prefix +
        ", errno=" +
        std::to_string(::mysql_errno(mysql_)) +
        ", error=" +
        ::mysql_error(mysql_);

    LOG_ERROR(last_error_);
}

}  // namespace tinyimx