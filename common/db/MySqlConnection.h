#pragma once

#include "common/config/ConfigTypes.h"

#include <mysql/mysql.h>

#include <cstdint>
#include <string>
#include <vector>

namespace tinyimx {
    struct MySqlQueryResult {
        std::vector<std::string> fields;
        std::vector<std::vector<std::string>> rows;


        std::size_t RowCount() const {
            return rows.size();
        }

        std::size_t FieldCount() const {
            return fields.size();
        }

        bool Empty() const {
            return rows.empty();
        }
    };

    // Partial acknowledgements survive server statement errors. Never includes
    // an outbox write or COMMIT: the caller must validate query_result first.
    struct MySqlBeginInsertQueryResult {
        bool begin_succeeded{false};
        bool insert_succeeded{false};
        bool query_succeeded{false};
        std::uint64_t insert_id{0};
        std::uint64_t affected_rows{0};
        MySqlQueryResult query_result;
    };

    struct MySqlTransactionStatement {
        std::string sql;  // Trusted, already escaped repository SQL only.
        bool query{false};
    };
    struct MySqlTransactionStatementResult {
        MySqlQueryResult rows;
        std::uint64_t insert_id{0};
        std::uint64_t affected_rows{0};
    };

    // Acknowledging the INSERT alone never means the transaction is durable.
    // Any failure after command_attempted requires read-only CID resolution;
    // the client cannot infer whether the server executed the trailing COMMIT.
    struct MySqlInsertCommitResult {
        bool command_attempted{false};
        bool insert_acknowledged{false};
        bool commit_acknowledged{false};
        std::uint64_t insert_id{0};
        std::uint64_t affected_rows{0};
    };

    class MySqlConnection {
        public:
            MySqlConnection();
            ~MySqlConnection();

            MySqlConnection(const MySqlConnection&) = delete;
            MySqlConnection& operator=(const MySqlConnection&) = delete;

            bool Connect(const MySqlConfig& config);

            void Close();

            bool IsConnected() const;

            bool Ping();

            // Candidate fast reuse still checks local transport readiness and
            // performs a wire PING at most500ms after the previous validation.
            // Never reads the socket, reconnects, or replays a business command.
            bool MayReuseWithoutPing() const noexcept;
            std::uint64_t PingAttempts() const noexcept { return ping_attempts_; }
            std::string ConnectionDescription() const;


            bool Execute(const std::string& sql);

            bool Query(const std::string& sql, MySqlQueryResult* result);

            // At most128 statements, 1MiB/wire packet, one row/query and 8MiB
            // aggregate results. Requires an existing transaction; never sends
            // BEGIN/COMMIT or replays. Drains exact typed results; any protocol/
            // server failure closes the uncertain session. Caller owns rollback
            // or read-only CID resolution. Earlier results are not durable ACKs.
            bool ExecuteTransactionStatements(
                const std::vector<MySqlTransactionStatement>& statements,
                std::vector<MySqlTransactionStatementResult>* results);

            // Explicit opt-in for trusted native-batch builders:1..64 rows per
            // query, original128 statements/1MiB packet/8MiB aggregate bounds.
            // The original API retains its one-row contract and exact symbol.
            bool ExecuteTransactionStatementsBoundedRows(
                const std::vector<MySqlTransactionStatement>& statements,
                std::vector<MySqlTransactionStatementResult>* results,
                std::size_t max_query_rows);
            // COM_QUERY API attempts of the grouped method only, not TCP packets.
            std::uint64_t GroupedStatementCommandAttempts() const noexcept;

            // Takes trusted INSERT/SELECT statements, never raw user SQL.
            // Fixed three-result protocol, at most one row; rejects active transaction.
            bool BeginInsertAndQuery(const std::string& insert_sql,
                                     const std::string& query_sql,
                                     MySqlBeginInsertQueryResult* result);

            static constexpr std::size_t kInsertCommitPacketBytes = 1024 * 1024;
            // One trusted repository INSERT followed by COMMIT in one bounded
            // COM_QUERY. Existing message identity must already be validated.
            // Exactly two OK results, no early success, no retries. Any wire,
            // server or result-boundary failure retires the uncertain session.
            bool InsertAndCommit(const std::string& insert_sql,
                                 MySqlInsertCommitResult* result);
            std::uint64_t InsertCommitCommandAttempts() const noexcept {
                return insert_commit_command_attempts_;
            }

            bool BeginTransaction();

            // Begin a read-only REPEATABLE READ transaction with an eager
            // consistent snapshot. Use this when several reads must describe
            // one logical point-in-time view (for example group send
            // authorization + recipient snapshot).
            bool BeginConsistentReadTransaction();

            bool Commit();

            bool Rollback();

            bool InTransaction() const;

            std::uint64_t LastInsertId() const;
            std::uint64_t AffectedRows() const;

            std::string EscapeString(const std::string& value);

            const std::string& LastError() const;

        private:
            void SetError(const std::string& error_message);
            void SetMysqlError(const std::string& prefix);

        private:
            MYSQL* mysql_{nullptr};
            bool connected_{false};
            bool transaction_active_{false};
            std::int64_t health_validated_ns_{0};
            std::uint64_t ping_attempts_{0};
            std::string last_error_;

            std::uint64_t last_insert_id_{0};
            std::uint64_t affected_rows_{0};
            std::uint64_t grouped_statement_command_attempts_{0};
            std::uint64_t insert_commit_command_attempts_{0};
        };

}
