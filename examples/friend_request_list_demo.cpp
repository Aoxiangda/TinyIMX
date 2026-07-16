#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/repository/FriendRequestRepository.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

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

void PrintRecords(
    const std::vector<
        tinyimx::FriendRequestRecord
    >& records
) {
    for (const auto& record : records) {
        std::cout
            << "request:"
            << " request_id=" << record.request_id
            << " from_user_id="
            << record.from_user_id
            << " to_user_id="
            << record.to_user_id
            << " request_message="
            << record.request_message
            << " request_status="
            << static_cast<std::uint32_t>(
                   record.request_status
               )
            << " created_at="
            << record.created_at
            << " handled_at="
            << record.handled_at
            << " updated_at="
            << record.updated_at
            << " from_username="
            << record.from_username
            << " from_nickname="
            << record.from_nickname
            << " from_avatar_url="
            << record.from_avatar_url
            << " from_user_status="
            << record.from_user_status
            << '\n';
    }
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
        << "========== Friend Request List Demo ==========\n";

    bool ok = true;

    if (!CleanupRequestPair(
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
            "hello from user10004"
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

    const auto incoming =
        repository.ListPendingIncomingRequests(
            10001,
            "",
            0,
            20
        );

    std::cout
        << "user10001 incoming_count="
        << incoming.size()
        << '\n';

    PrintRecords(incoming);

    ok = ok &&
         repository.LastError().empty();

    ok = ok &&
         incoming.size() == 1;

    if (!incoming.empty()) {
        const auto& record = incoming.front();

        ok = ok &&
             record.request_id ==
                 created.request_id;

        ok = ok &&
             record.from_user_id == 10004;

        ok = ok &&
             record.to_user_id == 10001;

        ok = ok &&
             record.request_message ==
                 "hello from user10004";

        ok = ok &&
             record.request_status ==
                 tinyimx::FriendRequestStatus::
                     kPending;

        ok = ok &&
             record.from_username ==
                 "user10004";

        ok = ok &&
             !record.created_at.empty();

        ok = ok &&
             record.handled_at.empty();

        const auto next_page =
            repository.ListPendingIncomingRequests(
                10001,
                record.created_at,
                record.request_id,
                20
            );

        std::cout
            << "next_page_count="
            << next_page.size()
            << '\n';

        ok = ok &&
             repository.LastError().empty();

        ok = ok &&
             next_page.empty();
    }

    const auto sender_incoming =
        repository.ListPendingIncomingRequests(
            10004,
            "",
            0,
            20
        );

    std::cout
        << "user10004 incoming_count="
        << sender_incoming.size()
        << '\n';

    ok = ok &&
         repository.LastError().empty();

    ok = ok &&
         sender_incoming.empty();

    const auto invalid =
        repository.ListPendingIncomingRequests(
            0,
            "",
            0,
            20
        );

    ok = ok &&
         invalid.empty();

    ok = ok &&
         !repository.LastError().empty();

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
            << "friend request list validation failed\n";
        return 1;
    }

    std::cout
        << "friend request list validation passed\n"
        << "==============================================\n";

    return 0;
}