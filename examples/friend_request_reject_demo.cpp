#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/repository/FriendRequestRepository.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

bool CleanupPair(
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

    if (!connection->BeginTransaction()) {
        return false;
    }

    const std::string delete_requests_sql =
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

    if (!connection->Execute(
            delete_requests_sql
        )) {
        connection->Rollback();
        return false;
    }

    const std::string delete_relations_sql =
        "DELETE FROM im_user_relations "
        "WHERE "
        "(user_id = " +
        std::to_string(user_a) +
        " AND peer_user_id = " +
        std::to_string(user_b) + ") "
        "OR "
        "(user_id = " +
        std::to_string(user_b) +
        " AND peer_user_id = " +
        std::to_string(user_a) + ")";

    if (!connection->Execute(
            delete_relations_sql
        )) {
        connection->Rollback();
        return false;
    }

    return connection->Commit();
}

bool VerifyRejectedWithoutRelations(
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

    tinyimx::MySqlQueryResult request_result;

    const std::string request_sql =
        "SELECT request_status, handled_at "
        "FROM im_friend_requests "
        "WHERE request_id = " +
        std::to_string(request_id);

    if (!connection->Query(
            request_sql,
            &request_result
        )) {
        return false;
    }

    if (request_result.rows.size() != 1 ||
        request_result.rows.front().size() < 2) {
        return false;
    }

    if (std::stoul(
            request_result.rows.front()[0]
        ) != 2) {
        return false;
    }

    if (request_result.rows.front()[1].empty()) {
        return false;
    }

    tinyimx::MySqlQueryResult relation_result;

    if (!connection->Query(
            "SELECT COUNT(*) "
            "FROM im_user_relations "
            "WHERE "
            "(user_id = 10001 "
            "AND peer_user_id = 10004) "
            "OR "
            "(user_id = 10004 "
            "AND peer_user_id = 10001)",
            &relation_result
        )) {
        return false;
    }

    return
        relation_result.rows.size() == 1 &&
        relation_result.rows.front().size() == 1 &&
        std::stoull(
            relation_result.rows.front()[0]
        ) == 0;
}

bool VerifyPendingWithoutRelations(
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

    tinyimx::MySqlQueryResult result;

    const std::string sql =
        "SELECT request_status, handled_at "
        "FROM im_friend_requests "
        "WHERE request_id = " +
        std::to_string(request_id);

    if (!connection->Query(sql, &result)) {
        return false;
    }

    return
        result.rows.size() == 1 &&
        result.rows.front().size() >= 2 &&
        std::stoul(
            result.rows.front()[0]
        ) == 0 &&
        result.rows.front()[1].empty();
}

void PrintRejectResult(
    const std::string& name,
    const tinyimx::RejectFriendRequestResult& result
) {
    std::cout
        << name
        << ": status="
        << tinyimx::RejectFriendRequestStatusToString(
               result.status
           )
        << " request_id="
        << result.request_id
        << " requester_user_id="
        << result.requester_user_id
        << " receiver_user_id="
        << result.receiver_user_id
        << " message="
        << result.message
        << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path =
        "config/gateway.json";

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
        << "========== Friend Request Reject Demo ==========\n";

    bool ok = true;

    if (!CleanupPair(
            &pool,
            10001,
            10004
        )) {
        std::cerr << "initial cleanup failed\n";
        ok = false;
    }

    tinyimx::FriendRequestRepository repository(
        &pool
    );

    const auto created =
        repository.CreateFriendRequest(
            10004,
            10001,
            "please add user10004"
        );

    std::cout
        << "create_status="
        << tinyimx::CreateFriendRequestStatusToString(
               created.status
           )
        << " request_id="
        << created.request_id
        << '\n';

    ok = ok &&
         created.status ==
             tinyimx::CreateFriendRequestStatus::
                 kCreated &&
         created.request_id != 0;

    /*
     * 非接收者不能拒绝。
     */
    const auto wrong_receiver =
        repository.RejectFriendRequest(
            created.request_id,
            10002
        );

    PrintRejectResult(
        "wrong_receiver",
        wrong_receiver
    );

    ok = ok &&
         wrong_receiver.status ==
             tinyimx::RejectFriendRequestStatus::
                 kNotRequestReceiver;

    ok = ok &&
         VerifyPendingWithoutRelations(
             &pool,
             created.request_id
         );

    /*
     * 正确接收者拒绝。
     */
    const auto rejected =
        repository.RejectFriendRequest(
            created.request_id,
            10001
        );

    PrintRejectResult(
        "rejected",
        rejected
    );

    ok = ok &&
         rejected.status ==
             tinyimx::RejectFriendRequestStatus::
                 kRejected;

    ok = ok &&
         rejected.requester_user_id == 10004;

    ok = ok &&
         rejected.receiver_user_id == 10001;

    ok = ok &&
         VerifyRejectedWithoutRelations(
             &pool,
             created.request_id
         );

    /*
     * 重复拒绝必须幂等。
     */
    const auto repeated =
        repository.RejectFriendRequest(
            created.request_id,
            10001
        );

    PrintRejectResult(
        "already_rejected",
        repeated
    );

    ok = ok &&
         repeated.status ==
             tinyimx::RejectFriendRequestStatus::
                 kAlreadyRejected;

    /*
     * 被拒绝后不能直接同意。
     */
    const auto accept_after_reject =
        repository.AcceptFriendRequest(
            created.request_id,
            10001
        );

    std::cout
        << "accept_after_reject: status="
        << tinyimx::AcceptFriendRequestStatusToString(
               accept_after_reject.status
           )
        << '\n';

    ok = ok &&
         accept_after_reject.status ==
             tinyimx::AcceptFriendRequestStatus::
                 kRequestNotPending;

    ok = ok &&
         VerifyRejectedWithoutRelations(
             &pool,
             created.request_id
         );

    const auto missing =
        repository.RejectFriendRequest(
            999999999,
            10001
        );

    PrintRejectResult(
        "request_not_found",
        missing
    );

    ok = ok &&
         missing.status ==
             tinyimx::RejectFriendRequestStatus::
                 kRequestNotFound;

    if (!CleanupPair(
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
            << "friend request reject validation failed\n";
        return 1;
    }

    std::cout
        << "friend request reject validation passed\n"
        << "================================================\n";

    return 0;
}