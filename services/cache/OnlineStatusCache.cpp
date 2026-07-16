#include "services/cache/OnlineStatusCache.h"

#include "common/logging/LogMacros.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <utility>

namespace tinyimx {
namespace {

using Json = nlohmann::json;

std::int64_t NowUnixSeconds() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()
        );

    return seconds.count();
}

}  // namespace

OnlineStatusCache::OnlineStatusCache(RedisConnectionPool* pool,
                                     std::string key_prefix)
    : pool_(pool),
      key_prefix_(std::move(key_prefix)) {}

bool OnlineStatusCache::SetOnline(
    std::uint64_t user_id,
    const std::string& gateway_id,
    const std::string& connection_name,
    int ttl_seconds
) {
    if (pool_ == nullptr) {
        SetError("online status set online failed: redis pool is null");
        return false;
    }

    if (user_id == 0) {
        SetError("online status set online failed: invalid user_id");
        return false;
    }

    if (gateway_id.empty()) {
        SetError("online status set online failed: gateway_id is empty");
        return false;
    }

    if (connection_name.empty()) {
        SetError("online status set online failed: connection_name is empty");
        return false;
    }

    if (ttl_seconds <= 0) {
        SetError("online status set online failed: invalid ttl_seconds");
        return false;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError("online status set online failed: acquire redis connection failed");
        return false;
    }

    OnlineStatusRecord record;
    record.user_id = user_id;
    record.gateway_id = gateway_id;
    record.connection_name = connection_name;
    record.login_time = NowUnixSeconds();

    const bool ok = connection->SetEx(
        BuildKey(user_id),
        Serialize(record),
        ttl_seconds
    );

    if (!ok) {
        SetError(connection->LastError());
        return false;
    }

    LOG_INFO("online status set"
             << ", user_id=" << user_id
             << ", gateway_id=" << gateway_id
             << ", connection=" << connection_name
             << ", ttl_seconds=" << ttl_seconds);

    last_error_.clear();
    return true;
}

bool OnlineStatusCache::SetOffline(std::uint64_t user_id) {
    if (pool_ == nullptr) {
        SetError("online status set offline failed: redis pool is null");
        return false;
    }

    if (user_id == 0) {
        SetError("online status set offline failed: invalid user_id");
        return false;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError("online status set offline failed: acquire redis connection failed");
        return false;
    }

    if (!connection->Del(BuildKey(user_id))) {
        SetError(connection->LastError());
        return false;
    }

    LOG_INFO("online status removed"
             << ", user_id=" << user_id);

    last_error_.clear();
    return true;
}

bool OnlineStatusCache::RefreshOnline(std::uint64_t user_id,
                                      int ttl_seconds) {
    if (pool_ == nullptr) {
        SetError("online status refresh failed: redis pool is null");
        return false;
    }

    if (user_id == 0) {
        SetError("online status refresh failed: invalid user_id");
        return false;
    }

    if (ttl_seconds <= 0) {
        SetError("online status refresh failed: invalid ttl_seconds");
        return false;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError("online status refresh failed: acquire redis connection failed");
        return false;
    }

    const bool ok = connection->Expire(
        BuildKey(user_id),
        ttl_seconds
    );

    if (!ok) {
        SetError(connection->LastError());
        return false;
    }

    LOG_INFO("online status refreshed"
             << ", user_id=" << user_id
             << ", ttl_seconds=" << ttl_seconds);

    last_error_.clear();
    return true;
}

std::optional<OnlineStatusRecord>
OnlineStatusCache::GetOnlineStatus(std::uint64_t user_id) {
    if (pool_ == nullptr) {
        SetError("online status get failed: redis pool is null");
        return std::nullopt;
    }

    if (user_id == 0) {
        SetError("online status get failed: invalid user_id");
        return std::nullopt;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError("online status get failed: acquire redis connection failed");
        return std::nullopt;
    }

    const auto value = connection->Get(BuildKey(user_id));
    if (!value.has_value()) {
        last_error_.clear();
        return std::nullopt;
    }

    return Deserialize(value.value());
}

bool OnlineStatusCache::IsOnline(std::uint64_t user_id) {
    return GetOnlineStatus(user_id).has_value();
}

const std::string& OnlineStatusCache::LastError() const {
    return last_error_;
}

std::string OnlineStatusCache::BuildKey(std::uint64_t user_id) const {
    return key_prefix_ + std::to_string(user_id);
}

std::string OnlineStatusCache::Serialize(
    const OnlineStatusRecord& record
) const {
    Json body;
    body["user_id"] = record.user_id;
    body["gateway_id"] = record.gateway_id;
    body["connection_name"] = record.connection_name;
    body["login_time"] = record.login_time;

    return body.dump();
}

std::optional<OnlineStatusRecord> OnlineStatusCache::Deserialize(
    const std::string& value
) {
    try {
        const Json body = Json::parse(value);

        OnlineStatusRecord record;
        record.user_id = body.at("user_id").get<std::uint64_t>();
        record.gateway_id = body.at("gateway_id").get<std::string>();
        record.connection_name =
            body.at("connection_name").get<std::string>();
        record.login_time = body.at("login_time").get<std::int64_t>();

        last_error_.clear();
        return record;
    } catch (const std::exception& e) {
        SetError(
            std::string("online status deserialize failed: ") +
            e.what()
        );
        return std::nullopt;
    }
}

void OnlineStatusCache::SetError(
    const std::string& error_message
) {
    last_error_ = error_message;
    LOG_ERROR(error_message);
}

}  // namespace tinyimx