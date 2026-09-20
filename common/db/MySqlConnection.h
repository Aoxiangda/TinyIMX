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

            bool Execute(const std::string& sql);

            bool Query(const std::string& sql, MySqlQueryResult* result);

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
            std::string last_error_;

            std::uint64_t last_insert_id_{0};
            std::uint64_t affected_rows_{0};
        };

}