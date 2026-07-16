#pragma once

#include "common/config/ConfigTypes.h"
#include "common/db/MySqlConnection.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>

namespace tinyimx {
    class MySqlConnectionPool;

    class MySqlConnectionLease {
        public:
            MySqlConnectionLease() = default;

            MySqlConnectionLease(
                MySqlConnectionPool* pool,
                std::unique_ptr<MySqlConnection> connection
            );

            ~MySqlConnectionLease();

            MySqlConnectionLease(const MySqlConnection&) = delete;
            MySqlConnectionLease& operator = (const MySqlConnection&) = delete;

            MySqlConnectionLease(MySqlConnectionLease&& other) noexcept;
            MySqlConnectionLease& operator = (
                MySqlConnectionLease&& other
            ) noexcept;

            MySqlConnection* operator->();
            const MySqlConnection* operator->() const;

            MySqlConnection& operator*();
            const MySqlConnection& operator*() const;

            bool IsValid() const;
            explicit operator bool() const;

            void Reset();

        private:
            MySqlConnectionPool* pool_{nullptr};
            std::unique_ptr<MySqlConnection> connection_;
    };

    class MySqlConnectionPool {
        public:
            MySqlConnectionPool() = default;
            ~MySqlConnectionPool();

            MySqlConnectionPool(const MySqlConnectionPool&) = delete;
            MySqlConnectionPool& operator=(const MySqlConnectionPool&) = delete;

            bool Initialize(const MySqlConfig& config);

            void Shutdown();

            MySqlConnectionLease Acquire(
                std::chrono::milliseconds timeout =
                    std::chrono::milliseconds(3000)
            );

            std::size_t Size() const;
            std::size_t AvailableCount() const;

        private:
            friend class MySqlConnectionLease;

            void Release(std::unique_ptr<MySqlConnection> connection);

        private:
            mutable std::mutex mutex_;
            std::condition_variable cv_;

            MySqlConfig config_;

            bool initialized_{false};
            bool shutting_down_{false};

            std::size_t size_{0};

            std::deque<std::unique_ptr<MySqlConnection>> connections_;
    };
}
