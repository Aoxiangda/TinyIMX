#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/repository/FriendRequestRepository.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

void PrintResult(
    const std::string& name,
    const tinyimx::CreateFriendRequestResult& result
) {
    std::cout
        << name
        << ": status="
        << tinyimx::CreateFriendRequestStatusToString(
               result.status
           )
        << " request_id="
        << result.request_id
        << " message="
        << result.message
        << '\n';
}

bool CleanupRequestPair(
    tinyimx::MySqlConnectionPool* pool,
    std::uint64_t user_a,
    std::uint64_t user_b
) {
    if (pool == nullptr) {
        return false;
    }

    auto connection = pool->Acquire();

    if (!connection) {
        return false;
    }

    const std::string sql =
        "DELETE FROM im_friend_requests "
        "WHERE "
        "(from_user_id = " +
        std::to_string(user_a) +
        " AND to_user_id = " +
        std::to_string(user_b) + ") "
        "OR "
        "(from_user_id = " +
        std::to_string(user_b) +
        " AND to_user_id = " +
        std::to_string(user_a) + ")";

    return connection->Execute(sql);
}

bool SetRequestRejected(
    tinyimx::MySqlConnectionPool* pool,
    std::uint64_t request_id
) {
    if (pool == nullptr || request_id == 0) {
        return false;
    }

    auto connection = pool->Acquire();

    if (!connection) {
        return false;
    }

    const std::string sql =
        "UPDATE im_friend_requests "
        "SET "
        "request_status = 2, "
        "handled_at = CURRENT_TIMESTAMP "
        "WHERE request_id = " +
        std::to_string(request_id);

    return connection->Execute(sql);
}

bool VerifyPendingRequest(
    tinyimx::MySqlConnectionPool* pool,
    std::uint64_t request_id,
    std::uint64_t from_user_id,
    std::uint64_t to_user_id,
    const std::string& expected_message
) {
    if (pool == nullptr || request_id == 0) {
        return false;
    }

    auto connection = pool->Acquire();

    if (!connection) {
        return false;
    }

    tinyimx::MySqlQueryResult result;

    const std::string sql =
        "SELECT "
        "from_user_id, "
        "to_user_id, "
        "request_message, "
        "request_status, "
        "handled_at "
        "FROM im_friend_requests "
        "WHERE request_id = " +
        std::to_string(request_id);

    if (!connection->Query(sql, &result)) {
        return false;
    }

    if (result.rows.size() != 1 ||
        result.rows.front().size() < 5) {
        return false;
    }

    const auto& row = result.rows.front();

    return std::stoull(row[0]) == from_user_id &&
           std::stoull(row[1]) == to_user_id &&
           row[2] == expected_message &&
           std::stoul(row[3]) == 0 &&
           row[4].empty();
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway.json";

    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;

    if (!config.LoadFromFile(config_path)) {
        std::cerr
            << "load config failed: "
            << config.LastError()
            << '\n';
        return 1;
    }

    if (!tinyimx::Logger::Instance().Init(
            config.Logger()
        )) {
        std::cerr << "logger init failed\n";
        return 1;
    }

    tinyimx::MySqlConnectionPool pool;

    if (!pool.Initialize(config.MySql())) {
        std::cerr << "mysql pool init failed\n";
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    std::cout
        << "========== Friend Request Create Demo ==========\n";

    bool ok = true;

    /*
     * 10001和10004当前没有关系，用于创建申请测试。
     */
    if (!CleanupRequestPair(
            &pool,
            10001,
            10004
        )) {
        std::cerr << "cleanup request pair failed\n";
        ok = false;
    }

    tinyimx::FriendRequestRepository repository(
        &pool
    );

    const auto self_result =
        repository.CreateFriendRequest(
            10001,
            10001,
            "self request"
        );

    PrintResult("self_request", self_result);

    ok = ok &&
         self_result.status ==
             tinyimx::CreateFriendRequestStatus::
                 kInvalidArgument;

    const auto friend_result =
        repository.CreateFriendRequest(
            10001,
            10002,
            "already friend"
        );

    PrintResult("already_friend", friend_result);

    ok = ok &&
         friend_result.status ==
             tinyimx::CreateFriendRequestStatus::
                 kAlreadyFriend;

    const auto blocked_result =
        repository.CreateFriendRequest(
            10001,
            10003,
            "blocked user"
        );

    PrintResult("blocked_by_self", blocked_result);

    ok = ok &&
         blocked_result.status ==
             tinyimx::CreateFriendRequestStatus::
                 kBlockedBySelf;

    const auto missing_result =
        repository.CreateFriendRequest(
            10001,
            999999,
            "missing user"
        );

    PrintResult("target_not_found", missing_result);

    ok = ok &&
         missing_result.status ==
             tinyimx::CreateFriendRequestStatus::
                 kTargetUserNotFound;

    const auto created_result =
        repository.CreateFriendRequest(
            10001,
            10004,
            "hello user10004"
        );

    PrintResult("created", created_result);

    ok = ok &&
         created_result.status ==
             tinyimx::CreateFriendRequestStatus::
                 kCreated &&
         created_result.request_id != 0;

    const auto repeated_result =
        repository.CreateFriendRequest(
            10001,
            10004,
            "duplicate request"
        );

    PrintResult("already_pending", repeated_result);

    ok = ok &&
         repeated_result.status ==
             tinyimx::CreateFriendRequestStatus::
                 kAlreadyPending &&
         repeated_result.request_id ==
             created_result.request_id;

    const auto reverse_result =
        repository.CreateFriendRequest(
            10004,
            10001,
            "reverse request"
        );

    PrintResult("reverse_pending", reverse_result);

    ok = ok &&
         reverse_result.status ==
             tinyimx::CreateFriendRequestStatus::
                 kReversePending &&
         reverse_result.request_id ==
             created_result.request_id;

    if (!SetRequestRejected(
            &pool,
            created_result.request_id
        )) {
        std::cerr << "set request rejected failed\n";
        ok = false;
    }

    const auto reopened_result =
        repository.CreateFriendRequest(
            10001,
            10004,
            "retry user10004"
        );

    PrintResult("reopened", reopened_result);

    ok = ok &&
         reopened_result.status ==
             tinyimx::CreateFriendRequestStatus::
                 kReopened &&
         reopened_result.request_id ==
             created_result.request_id;

    ok = ok &&
         VerifyPendingRequest(
             &pool,
             reopened_result.request_id,
             10001,
             10004,
             "retry user10004"
         );

    if (!CleanupRequestPair(
            &pool,
            10001,
            10004
        )) {
        std::cerr << "final cleanup failed\n";
        ok = false;
    }

    pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();

    if (!ok) {
        std::cerr
            << "friend request create validation failed\n";
        return 1;
    }

    std::cout
        << "friend request create validation passed\n"
        << "================================================\n";

    return 0;
}