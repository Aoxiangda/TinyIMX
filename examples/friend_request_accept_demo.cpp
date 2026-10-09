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
        ) != 0) {
        return false;
    }

    if (!request_result.rows.front()[1].empty()) {
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

bool VerifyAcceptedWithRelations(
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
        ) != 1) {
        return false;
    }

    if (request_result.rows.front()[1].empty()) {
        return false;
    }

    tinyimx::MySqlQueryResult relation_result;

    if (!connection->Query(
            "SELECT COUNT(*) "
            "FROM im_user_relations "
            "WHERE relation_status = 1 "
            "AND ("
            "(user_id = 10001 "
            "AND peer_user_id = 10004) "
            "OR "
            "(user_id = 10004 "
            "AND peer_user_id = 10001)"
            ")",
            &relation_result
        )) {
        return false;
    }

    return
        relation_result.rows.size() == 1 &&
        relation_result.rows.front().size() == 1 &&
        std::stoull(
            relation_result.rows.front()[0]
        ) == 2;
}

void PrintAcceptResult(
    const std::string& name,
    const tinyimx::AcceptFriendRequestResult& result
) {
    std::cout
        << name
        << ": status="
        << tinyimx::AcceptFriendRequestStatusToString(
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
        << "========== Friend Request Accept Demo ==========\n";

    bool ok = true;

    /*
     * 10001与10004是本Demo控制的测试用户对。
     * 当前正式基线中双方没有关系。
     */
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
            "please accept user10004"
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
     * user10002不是申请接收者，不能处理。
     */
    const auto wrong_receiver =
        repository.AcceptFriendRequest(
            created.request_id,
            10002
        );

    PrintAcceptResult(
        "wrong_receiver",
        wrong_receiver
    );

    ok = ok &&
         wrong_receiver.status ==
             tinyimx::AcceptFriendRequestStatus::
                 kNotRequestReceiver;

    ok = ok &&
         VerifyPendingWithoutRelations(
             &pool,
             created.request_id
         );

    /*
     * 真正接收者user10001同意申请。
     */
    const auto accepted =
        repository.AcceptFriendRequest(
            created.request_id,
            10001
        );

    PrintAcceptResult(
        "accepted",
        accepted
    );

    ok = ok &&
         accepted.status ==
             tinyimx::AcceptFriendRequestStatus::
                 kAccepted;

    ok = ok &&
         accepted.requester_user_id == 10004;

    ok = ok &&
         accepted.receiver_user_id == 10001;

    ok = ok &&
         VerifyAcceptedWithRelations(
             &pool,
             created.request_id
         );

    /*
     * 重复请求应当幂等返回already_accepted。
     */
    const auto repeated =
        repository.AcceptFriendRequest(
            created.request_id,
            10001
        );

    PrintAcceptResult(
        "already_accepted",
        repeated
    );

    ok = ok &&
         repeated.status ==
             tinyimx::AcceptFriendRequestStatus::
                 kAlreadyAccepted;

    ok = ok &&
         VerifyAcceptedWithRelations(
             &pool,
             created.request_id
         );

    const auto missing =
        repository.AcceptFriendRequest(
            999999999,
            10001
        );

    PrintAcceptResult(
        "request_not_found",
        missing
    );

    ok = ok &&
         missing.status ==
             tinyimx::AcceptFriendRequestStatus::
                 kRequestNotFound;

    if (!CleanupPair(
            &pool,
            10001,
            10004
        )) {
        std::cerr << "final cleanup failed\n";
        ok = false;
    }

    tinyimx::FriendRequestRepository
        unavailable_repository(
            nullptr
        );

    const auto storage_error_result =
        unavailable_repository.
            AcceptFriendRequest(
                1,
                10001
            );

    PrintAcceptResult(
        "storage_error",
        storage_error_result
    );

    ok = ok &&
        storage_error_result.status ==
            tinyimx::
                AcceptFriendRequestStatus::
                    kStorageError;

    ok = ok &&
        !storage_error_result.message.empty();
    pool.Shutdown();

    tinyimx::Logger::Instance().Shutdown();

    if (!ok) {
        std::cerr
            << "friend request accept validation failed\n";
        return 1;
    }

    std::cout
        << "friend request accept validation passed\n"
        << "================================================\n";

    return 0;
}