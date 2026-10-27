#include "common/db/MySqlConnection.h"

#include "common/logging/LogMacros.h"

#include <cstdlib>
#include <memory>
#include <chrono>
#if defined(__linux__) && defined(MYSQL_VERSION_ID) && MYSQL_VERSION_ID==80040
#include <poll.h>
#endif

namespace tinyimx {
namespace {
std::int64_t HealthNowNs() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

bool MySqlConnection::MayReuseWithoutPing() const noexcept {
#if defined(__linux__) && defined(MYSQL_VERSION_ID) && MYSQL_VERSION_ID==80040
    if (!IsConnected() || transaction_active_ || health_validated_ns_<=0) return false;
    // Unconsumed protocol results always take the original probe/reconnect path,
    // even when remaining packets have already entered the client-library buffer.
    if (mysql_->status!=MYSQL_STATUS_READY || ::mysql_more_results(mysql_)) return false;
    const auto age=HealthNowNs()-health_validated_ns_;
    if(age<0 || age>=500000000) return false;
    // The pinned Oracle8.0.40 header exposes NET.fd (also used by DBI).
    // Other client headers take the original wire-PING path conservatively.
    const auto socket=mysql_->net.fd;
    if(socket<0) return false;
    pollfd descriptor{socket,POLLIN,0};
    // EOF, pending unsolicited data, error, HUP, invalid fd or interrupted poll
    // always take the original PING/reconnect path, with no socket consumption.
    return ::poll(&descriptor,1,0)==0;
#else
    return false;
#endif
}

std::string MySqlConnection::ConnectionDescription() const {
    if (!IsConnected()) return {};
    const char* description=::mysql_get_host_info(mysql_);
    return description?description:"";
}

std::uint64_t MySqlConnection::GroupedStatementCommandAttempts() const noexcept {
    return grouped_statement_command_attempts_;
}

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
    // Close and failed Connect intentionally clear the handle. Always allocate
    // a new one so a transient failure cannot make this object unrecoverable.
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

    const bool local_socket=!config.unix_socket.empty();
    if (local_socket && (config.unix_socket.front()!='/' ||
        config.unix_socket.size()>107 || config.unix_socket.find('\0')!=std::string::npos)) {
        SetError("mysql connect failed: invalid local socket path");
        Close();return false;
    }
    if (local_socket) {
        const unsigned int protocol=MYSQL_PROTOCOL_SOCKET;
        if (::mysql_options(mysql_,MYSQL_OPT_PROTOCOL,&protocol)!=0) {
            SetMysqlError("mysql local socket protocol selection failed");
            Close();return false;
        }
    }
    MYSQL* result = ::mysql_real_connect(
        mysql_,
        local_socket?"localhost":config.host.c_str(),
        config.user.c_str(),
        config.password.c_str(),
        config.database.c_str(),
        local_socket?0:static_cast<unsigned int>(config.port),
        local_socket?config.unix_socket.c_str():nullptr,
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
    health_validated_ns_ = HealthNowNs();

    last_insert_id_ = 0;
    affected_rows_ = 0;

    last_error_.clear();

    LOG_INFO("mysql connected"
             << ", transport=" << ConnectionDescription()
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
    health_validated_ns_ = 0;

    last_insert_id_ = 0;
    affected_rows_ = 0;
}

bool MySqlConnection::IsConnected() const {
    return connected_ && mysql_ != nullptr;
}

bool MySqlConnection::Ping() {
    ++ping_attempts_;
    if (!IsConnected()) {
        SetError("mysql ping failed: not connected");
        return false;
    }

    if (::mysql_ping(mysql_) != 0) {
        SetMysqlError("mysql_ping failed");
        connected_ = false;
        return false;
    }

    health_validated_ns_ = HealthNowNs();
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

bool MySqlConnection::BeginInsertAndQuery(
    const std::string& insert_sql,
    const std::string& query_sql,
    MySqlBeginInsertQueryResult* result
) {
    if (result == nullptr) {
        SetError("mysql begin insert query failed: result is null");
        return false;
    }
    *result = {};
    if (!IsConnected() || transaction_active_ || insert_sql.empty() || query_sql.empty()) {
        SetError("mysql begin insert query failed: unavailable connection or active transaction or empty SQL");
        return false;
    }
    // A protocol/client failure may leave results unread. Closing this session
    // rolls back the uncommitted write at the server; never send COMMIT or
    // attempt ROLLBACK into a stream whose result boundary is unknown.
    const auto discard_session = [&]() {
        transaction_active_ = false;
        Close();
    };
    const auto statement_error = [&](const char* phase) {
        SetMysqlError(phase);
        if (::mysql_errno(mysql_) >= 2000) discard_session();
    };
    const std::string sql = "START TRANSACTION;" + insert_sql + ";" + query_sql;
    if (::mysql_real_query(mysql_, sql.data(), sql.size()) != 0) {
        SetMysqlError("mysql begin insert query first result failed");
        // No acknowledged BEGIN. A lost response still cannot become durable,
        // since this packet never contains COMMIT. Retire uncertain sessions.
        discard_session();
        return false;
    }
    try {
        for (std::size_t statement = 0; statement < 3; ++statement) {
            std::unique_ptr<MYSQL_RES, decltype(&::mysql_free_result)>
                rows(::mysql_store_result(mysql_), ::mysql_free_result);
            if (statement < 2) {
                if (rows || ::mysql_field_count(mysql_) != 0) {
                    rows.reset();
                    SetError("mysql begin insert query failed: unexpected write result");
                    discard_session();
                    return false;
                }
                if (statement == 0) {
                    if (!(mysql_->server_status & SERVER_STATUS_IN_TRANS)) {
                        SetError("mysql begin insert query failed: BEGIN not active");
                        discard_session();
                        return false;
                    }
                    result->begin_succeeded = true;
                    transaction_active_ = true;
                } else {
                    const auto affected = ::mysql_affected_rows(mysql_);
                    result->affected_rows = affected == static_cast<my_ulonglong>(-1)
                        ? 0 : static_cast<std::uint64_t>(affected);
                    result->insert_id = ::mysql_insert_id(mysql_);
                    result->insert_succeeded = true;
                }
            } else {
                if (!rows || ::mysql_num_rows(rows.get()) > 1 ||
                    ::mysql_num_fields(rows.get()) > 64) {
                    rows.reset();
                    SetError("mysql begin insert query failed: unexpected query result");
                    discard_session();
                    return false;
                }
                const auto count = ::mysql_num_fields(rows.get());
                const auto fields = ::mysql_fetch_fields(rows.get());
                for (unsigned i = 0; i < count; ++i)
                    result->query_result.fields.emplace_back(fields[i].name);
                MYSQL_ROW row = nullptr;
                while ((row = ::mysql_fetch_row(rows.get()))) {
                    const auto lengths = ::mysql_fetch_lengths(rows.get());
                    std::vector<std::string> values;
                    values.reserve(count);
                    for (unsigned i = 0; i < count; ++i)
                        values.emplace_back(row[i] ? std::string(row[i], lengths[i]) : "");
                    result->query_result.rows.push_back(std::move(values));
                }
                if (::mysql_errno(mysql_)) {
                    rows.reset();
                    SetMysqlError("mysql begin insert query fetch failed");
                    discard_session();
                    return false;
                }
            }
            rows.reset();
            const auto next = ::mysql_next_result(mysql_);
            if (next > 0) {
                statement_error("mysql begin insert query next result failed");
                return false;
            }
            if ((statement < 2 && next != 0) || (statement == 2 && next != -1)) {
                SetError("mysql begin insert query failed: expected exactly three results");
                discard_session();
                return false;
            }
        }
        if (!(mysql_->server_status & SERVER_STATUS_IN_TRANS)) {
            SetError("mysql begin insert query failed: transaction no longer active");
            discard_session();
            return false;
        }
        result->query_succeeded = true;
        last_insert_id_ = result->insert_id;
        affected_rows_ = result->affected_rows;
        last_error_.clear();
        return true;
    } catch (...) {
        // Includes allocation exceptions while materializing the one row.
        discard_session();
        throw;
    }
}

bool MySqlConnection::ExecuteTransactionStatements(
    const std::vector<MySqlTransactionStatement>& statements,
    std::vector<MySqlTransactionStatementResult>* results
) {
    return ExecuteTransactionStatementsBoundedRows(statements, results, 1);
}

bool MySqlConnection::ExecuteTransactionStatementsBoundedRows(
    const std::vector<MySqlTransactionStatement>& statements,
    std::vector<MySqlTransactionStatementResult>* results,
    std::size_t max_query_rows
) {
    constexpr std::size_t kPacketBytes=1024*1024,kResultBytes=8*1024*1024;
    if (!results) {SetError("transaction statements require a result output");return false;}
    results->clear();
    if (!IsConnected()||!InTransaction()||statements.empty()||statements.size()>128 ||
        max_query_rows == 0 || max_query_rows > 64) {
        SetError("transaction statements require active transaction and bounded input");return false;
    }
    std::vector<std::string> sql;sql.reserve(statements.size());
    for (const auto& statement:statements) {
        auto text=statement.sql;
        while(!text.empty()&&(text.back()==';'||text.back()==' '||text.back()=='\r'||text.back()=='\n'||text.back()=='\t'))text.pop_back();
        if(text.empty()||text.size()+1>kPacketBytes){SetError("transaction statement exceeds packet bound");return false;}
        sql.push_back(std::move(text));
    }
    const auto close_failed=[&](const std::string& reason){
        const auto error=reason.empty()?LastError():reason;
        Close();SetError(error);return false;
    };
    std::size_t result_bytes=0;
    try {
        for(std::size_t first=0;first<sql.size();) {
            std::string packet;std::size_t last=first;
            while(last<sql.size()&&packet.size()+sql[last].size()+1<=kPacketBytes) {
                packet+=sql[last++];packet+=';';
            }
            ++grouped_statement_command_attempts_;
            if(::mysql_real_query(mysql_,packet.data(),static_cast<unsigned long>(packet.size()))!=0) {
                SetMysqlError("transaction statement packet failed");return close_failed("");
            }
            for(std::size_t i=first;i<last;++i) {
                MySqlTransactionStatementResult item;
                std::unique_ptr<MYSQL_RES,decltype(&::mysql_free_result)> stored(::mysql_store_result(mysql_),::mysql_free_result);
                const auto fail_item=[&](const std::string& reason){stored.reset();return close_failed(reason);};
                if(statements[i].query) {
                    if(!stored)return fail_item("transaction packet missing expected query result");
                    const auto count=::mysql_num_fields(stored.get());
                    if(count>16||::mysql_num_rows(stored.get())>max_query_rows)
                        return fail_item(max_query_rows == 1
                            ? "transaction query exceeds single-record bound"
                            : "transaction query exceeds configured row bound");
                    const auto* fields=::mysql_fetch_fields(stored.get());
                    for(unsigned k=0;k<count;++k)item.rows.fields.emplace_back(fields[k].name);
                    MYSQL_ROW row;
                    while((row=::mysql_fetch_row(stored.get()))!=nullptr) {
                        auto* lengths=::mysql_fetch_lengths(stored.get());
                        if(!lengths)return fail_item("transaction query lengths unavailable");
                        std::vector<std::string> values;values.reserve(count);
                        for(unsigned k=0;k<count;++k) {
                            const auto size=row[k]?lengths[k]:0;
                            result_bytes+=size;
                            if(result_bytes>kResultBytes)return fail_item("transaction query aggregate result bound");
                            values.emplace_back(row[k]?std::string(row[k],size):std::string{});
                        }
                        item.rows.rows.push_back(std::move(values));
                    }
                    if(::mysql_errno(mysql_)!=0){SetMysqlError("transaction fetch failed");return fail_item("");}
                } else if(stored||::mysql_field_count(mysql_)!=0) {
                    return fail_item("transaction packet unexpected query result");
                }
                const auto affected=::mysql_affected_rows(mysql_);
                if(affected==static_cast<my_ulonglong>(-1))return fail_item("transaction affected-row result unavailable");
                item.affected_rows=affected;item.insert_id=::mysql_insert_id(mysql_);
                // Internal builders contain DML/SELECT only. Never silently
                // accept an implicit COMMIT/DDL or an already-rolled-back session.
                if(!(mysql_->server_status&SERVER_STATUS_IN_TRANS))
                    return fail_item("transaction packet unexpectedly left transaction");
                last_insert_id_=item.insert_id;affected_rows_=item.affected_rows;
                stored.reset();results->push_back(std::move(item));
                const int next=::mysql_next_result(mysql_);
                if(next>0){SetMysqlError("transaction packet next statement failed");return close_failed("");}
                if((i+1<last&&next!=0)||(i+1==last&&next!=-1))
                    return close_failed("transaction packet result count mismatch");
            }
            first=last;
        }
    } catch (...) {
        close_failed("transaction packet parsing failed");throw;
    }
    last_error_.clear();return true;
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

bool MySqlConnection::InsertAndCommit(
    const std::string& insert_sql, MySqlInsertCommitResult* result) {
    if (!result) {
        SetError("mysql insert commit requires a result output");
        return false;
    }
    *result = {};
    constexpr char kCommit[] = ";COMMIT";
    if (!IsConnected() || !transaction_active_ ||
        !(mysql_->server_status & SERVER_STATUS_IN_TRANS) ||
        insert_sql.compare(0, 12, "INSERT INTO ") != 0 ||
        insert_sql.size() > kInsertCommitPacketBytes - (sizeof(kCommit) - 1)) {
        SetError("mysql insert commit requires an active transaction and bounded trusted INSERT");
        return false;
    }
    const std::string packet = insert_sql + kCommit;
    const auto failed = [&](std::string message) {
        // A result may have been lost after the server committed. Never replay
        // this packet or send another command on an uncertain result stream.
        Close();
        SetError(message);
        return false;
    };
    result->command_attempted = true;
    ++insert_commit_command_attempts_;
    if (::mysql_real_query(mysql_, packet.data(), packet.size()) != 0) {
        SetMysqlError("mysql insert commit first result failed");
        return failed(LastError());
    }
    try {
        for (unsigned index = 0; index < 2; ++index) {
            std::unique_ptr<MYSQL_RES, decltype(&::mysql_free_result)>
                rows(::mysql_store_result(mysql_), ::mysql_free_result);
            if (rows || ::mysql_field_count(mysql_) != 0 || ::mysql_errno(mysql_) != 0) {
                rows.reset();
                return failed("mysql insert commit expected a write OK result");
            }
            const auto affected = ::mysql_affected_rows(mysql_);
            if (index == 0) {
                const auto id = ::mysql_insert_id(mysql_);
                if (affected != 1 || id == 0 ||
                    !(mysql_->server_status & SERVER_STATUS_IN_TRANS)) {
                    return failed("mysql insert commit invalid INSERT result");
                }
                result->insert_acknowledged = true;
                result->insert_id = id;
                result->affected_rows = affected;
            } else {
                if (affected != 0 || (mysql_->server_status & SERVER_STATUS_IN_TRANS)) {
                    return failed("mysql insert commit invalid COMMIT result");
                }
                result->commit_acknowledged = true;
            }
            rows.reset();
            const auto next = ::mysql_next_result(mysql_);
            if (next > 0) {
                SetMysqlError("mysql insert commit next result failed");
                return failed(LastError());
            }
            if ((index == 0 && next != 0) || (index == 1 && next != -1)) {
                return failed("mysql insert commit expected exactly two results");
            }
        }
        transaction_active_ = false;
        last_insert_id_ = 0;
        affected_rows_ = 0;
        last_error_.clear();
        return true;
    } catch (...) {
        Close();
        throw;
    }
}

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

    const auto code=::mysql_errno(mysql_);
    if(code==2006 || code==2013 || code==2014 || code==2055) {
        connected_=false;
        health_validated_ns_=0;
    }
    LOG_ERROR(last_error_);
}

}  // namespace tinyimx
