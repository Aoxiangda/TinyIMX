#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/outbox/OutboxRepository.h"
#include "services/repository/MessageRepository.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr std::uint64_t kSender = 10001;
constexpr std::uint64_t kU2 = 10002;
constexpr std::uint64_t kU3 = 10003;
int g_failed = 0;

void Expect(bool condition, const std::string& label) {
    if (condition) std::cout << "[PASS] " << label << '\n';
    else { std::cerr << "[FAIL] " << label << '\n'; ++g_failed; }
}

std::string Unique(const char* prefix) {
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return std::string(prefix) + std::to_string(static_cast<long long>(now));
}

bool TableExists(tinyimx::MySqlConnectionPool* pool, const std::string& table) {
    if (pool == nullptr) return false;
    auto c = pool->Acquire();
    if (!c) return false;
    tinyimx::MySqlQueryResult rows;
    return c->Query(
        "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='" +
        c->EscapeString(table) + "'", &rows) &&
        rows.rows.size() == 1 && rows.rows.front().size() == 1 && rows.rows.front().front() == "1";
}

bool UsersExist(tinyimx::MySqlConnectionPool* pool) {
    auto c = pool->Acquire(); if (!c) return false;
    tinyimx::MySqlQueryResult rows;
    return c->Query(
        "SELECT COUNT(*) FROM im_users WHERE user_id IN (10001,10002,10003)", &rows) &&
        rows.rows.size() == 1 && rows.rows.front().size() == 1 && rows.rows.front().front() == "3";
}

std::uint64_t CreateGroup(tinyimx::MySqlConnectionPool* pool, const std::string& name) {
    auto c = pool->Acquire(); if (!c) return 0;
    if (!c->Execute(
        "INSERT INTO im_groups (name,description,avatar_url,owner_user_id,status,join_policy,max_members,version,member_version) "
        "VALUES ('" + c->EscapeString(name) + "','','',10001,1,1,20,1,3)")) return 0;
    const auto id = c->LastInsertId();
    const std::string prefix =
        "INSERT INTO im_group_members (group_id,user_id,role,status,membership_epoch) VALUES ";
    const std::string values =
        "(" + std::to_string(id) + ",10001,1,1,1)," +
        "(" + std::to_string(id) + ",10002,3,1,1)," +
        "(" + std::to_string(id) + ",10003,3,1,1)";
    if (!c->Execute(prefix + values)) return 0;
    return id;
}

std::uint64_t Count(tinyimx::MySqlConnectionPool* pool, const std::string& sql) {
    auto c=pool->Acquire(); if(!c) return 0; tinyimx::MySqlQueryResult rows;
    if(!c->Query(sql,&rows)||rows.rows.size()!=1||rows.rows.front().size()!=1) return 0;
    return static_cast<std::uint64_t>(std::stoull(rows.rows.front().front()));
}

void Cleanup(tinyimx::MySqlConnectionPool* pool, std::uint64_t group_id) {
    if (!pool || group_id == 0) return;
    auto c=pool->Acquire(); if(!c) return;
    c->Execute("DELETE FROM im_group_message_deliveries WHERE group_id=" + std::to_string(group_id));
    c->Execute("DELETE FROM im_event_outbox WHERE event_type='group_message.created.v1' AND aggregate_type='group_message' AND CAST(aggregate_id AS UNSIGNED) IN (SELECT message_id FROM im_group_messages WHERE group_id=" + std::to_string(group_id) + ")");
    c->Execute("DELETE FROM im_group_messages WHERE group_id=" + std::to_string(group_id));
    c->Execute("DELETE FROM im_group_members WHERE group_id=" + std::to_string(group_id));
    c->Execute("DELETE FROM im_group_membership_history WHERE group_id=" + std::to_string(group_id));
    c->Execute("DELETE FROM im_groups WHERE group_id=" + std::to_string(group_id));
}
}  // namespace

int main(int argc, char* argv[]) {
    const std::string config_path = argc >= 2 ? argv[1] : "config/gateway-a.local.json";
    tinyimx::Config config;
    if (!config.LoadFromFile(config_path) || !config.MySql().enable) {
        std::cerr << "[FAIL] config/MySQL unavailable\n"; return 1;
    }
    if (!tinyimx::Logger::Instance().Init(config.Logger())) return 1;
    tinyimx::MySqlConnectionPool pool;
    if (!pool.Initialize(config.MySql())) { tinyimx::Logger::Instance().Shutdown(); return 1; }

    std::cout << "========== TinyIMX M17-B2 Group Fanout Repository Integration ==========\n";
    Expect(TableExists(&pool,"im_group_messages"), "im_group_messages exists");
    Expect(TableExists(&pool,"im_group_message_deliveries"), "im_group_message_deliveries exists");
    Expect(UsersExist(&pool), "fixture users 10001/10002/10003 exist");
    if (g_failed != 0) { pool.Shutdown(); tinyimx::Logger::Instance().Shutdown(); return 1; }

    const auto group_id = CreateGroup(&pool, Unique("m17b2-fanout-"));
    Expect(group_id != 0, "fanout fixture group created");
    if (group_id == 0) { pool.Shutdown(); tinyimx::Logger::Instance().Shutdown(); return 1; }

    tinyimx::MessageRepository repository(&pool);
    tinyimx::outbox::OutboxRepository outbox(&pool);
    tinyimx::message::MessageRepositoryAdapter adapter(&repository,&pool,&outbox);
    const std::string client_id = Unique("m17b2-client-");
    const std::vector<std::uint64_t> recipients{kU2,kU3};
    const auto created = adapter.PersistAuthorizedGroupMessage(
        kSender, group_id, client_id, 1, "m17-b2-fanout", 1, 3, 1, recipients);
    Expect(
        created.Completed() &&
        created.outcome ==
            tinyimx::message::PersistGroupMessageOutcome::kCreated &&
        created.message_id != 0,
        "message + recipient rows + outbox transaction created");
    if (created.message_id != 0) {
        const auto mid = created.message_id;
        Expect(Count(&pool,"SELECT COUNT(*) FROM im_group_message_deliveries WHERE message_id="+std::to_string(mid))==2,
               "exactly two durable recipient rows");
        Expect(Count(&pool,"SELECT COUNT(*) FROM im_group_message_deliveries WHERE message_id="+std::to_string(mid)+" AND recipient_user_id=10001")==0,
               "sender excluded from durable fanout");
        Expect(Count(&pool,"SELECT COUNT(*) FROM im_event_outbox WHERE event_id='group_message.created.v1:"+std::to_string(mid)+"'")==1,
               "fanout write retains exactly one outbox event");

        const auto reused = adapter.PersistAuthorizedGroupMessage(
            kSender, group_id, client_id, 1, "m17-b2-fanout", 2, 4, 2, {kU2});
        Expect(
            reused.Completed() &&
            reused.outcome ==
                tinyimx::message::PersistGroupMessageOutcome::kReused &&
            reused.message_id == mid,
            "durable retry reuses original message and recipient snapshot");
        Expect(Count(&pool,"SELECT COUNT(*) FROM im_group_message_deliveries WHERE message_id="+std::to_string(mid))==2,
               "reused send does not rewrite recipient snapshot");

        const auto claim_a = repository.ClaimGroupMessageDeliveries("gateway-a","lease-a",1,1000,mid);
        Expect(claim_a.Succeeded() && claim_a.records.size()==1,
               "first coordinator atomically claims one recipient lease");
        const auto claim_b = repository.ClaimGroupMessageDeliveries("gateway-b","lease-b",1,1000,mid);
        Expect(claim_b.Succeeded() && claim_b.records.size()==1 &&
               claim_a.records.front().delivery.recipient_user_id != claim_b.records.front().delivery.recipient_user_id,
               "SKIP LOCKED prevents concurrent coordinators claiming same recipient");

        std::uint64_t deferred_recipient = 0;
        if (!claim_a.records.empty()) {
            const auto recipient=claim_a.records.front().delivery.recipient_user_id;
            const auto complete=repository.CompleteGroupMessageDeliveryAttempt(
                mid,recipient,"lease-a",tinyimx::GroupDeliveryStatus::kPending,
                "gateway-a",100,"awaiting_receiver_ack");
            Expect(complete.Succeeded() && complete.affected_rows==1,
                   "submitted attempt releases lease and schedules durable retry");
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            const auto recovery=repository.ClaimGroupMessageDeliveries(
                "gateway-c","lease-c",1,1000,mid);
            Expect(recovery.Succeeded() && recovery.records.size()==1 &&
                   recovery.records.front().delivery.recipient_user_id==recipient,
                   "expired/retry-due recipient is reclaimable after coordinator uncertainty");
            if (!recovery.records.empty()) {
                const auto offline=repository.CompleteGroupMessageDeliveryAttempt(
                    mid,recipient,"lease-c",tinyimx::GroupDeliveryStatus::kDeferredOffline,
                    "gateway-c",0,"recipient_offline");
                Expect(offline.Succeeded() && offline.affected_rows==1,
                       "confirmed offline attempt becomes DEFERRED_OFFLINE");
                if (offline.Succeeded() && offline.affected_rows == 1) {
                    deferred_recipient = recipient;
                }
            }
        }

        if (deferred_recipient != 0) {
            const auto replay = repository.ClaimGroupMessageDeliveriesForRecipient(
                deferred_recipient, "gateway-replay-a", "replay-lease-a", 100, 10000);
            const bool replay_scope_valid =
                replay.Succeeded() &&
                std::all_of(
                    replay.records.begin(), replay.records.end(),
                    [&](const auto& item) {
                        return item.delivery.recipient_user_id == deferred_recipient &&
                               item.delivery.delivery_status ==
                                   tinyimx::GroupDeliveryStatus::kDeferredOffline &&
                               item.delivery.lease_token == "replay-lease-a";
                    });
            const bool replay_contains_current =
                std::any_of(
                    replay.records.begin(), replay.records.end(),
                    [&](const auto& item) {
                        return item.delivery.message_id == mid &&
                               item.delivery.recipient_user_id == deferred_recipient;
                    });
            Expect(replay_scope_valid && replay_contains_current,
                   "recipient reconnect claims current DEFERRED_OFFLINE durable delivery");

            const auto competing = repository.ClaimGroupMessageDeliveriesForRecipient(
                deferred_recipient, "gateway-replay-b", "replay-lease-b", 100, 10000);
            const bool competing_contains_current =
                std::any_of(
                    competing.records.begin(), competing.records.end(),
                    [&](const auto& item) {
                        return item.delivery.message_id == mid &&
                               item.delivery.recipient_user_id == deferred_recipient;
                    });
            Expect(competing.Succeeded() && !competing_contains_current,
                   "active replay lease prevents concurrent Gateway double claim for current delivery");

            const auto delivered = repository.ConfirmGroupMessageDelivery(mid, deferred_recipient);
            Expect(delivered.Succeeded() && delivered.affected_rows==1,
                   "replayed receiver ACK advances DEFERRED_OFFLINE to DELIVERED");
            const auto after_delivery = repository.ClaimGroupMessageDeliveriesForRecipient(
                deferred_recipient, "gateway-replay-c", "replay-lease-c", 100, 10000);
            const bool after_delivery_contains_current =
                std::any_of(
                    after_delivery.records.begin(), after_delivery.records.end(),
                    [&](const auto& item) {
                        return item.delivery.message_id == mid &&
                               item.delivery.recipient_user_id == deferred_recipient;
                    });
            Expect(after_delivery.Succeeded() && !after_delivery_contains_current,
                   "DELIVERED current group row is excluded from future replay claims");
        }

        if (!claim_b.records.empty()) {
            const auto recipient=claim_b.records.front().delivery.recipient_user_id;
            const auto confirmed=repository.ConfirmGroupMessageDelivery(mid,recipient);
            Expect(confirmed.Succeeded() && confirmed.affected_rows==1,
                   "receiver ACK advances durable row to DELIVERED");
            const auto duplicate=repository.ConfirmGroupMessageDelivery(mid,recipient);
            Expect(duplicate.Succeeded() && duplicate.affected_rows==0,
                   "duplicate receiver ACK is idempotent");
        }
    }

    Cleanup(&pool,group_id);
    pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();
    std::cout << "failed=" << g_failed << '\n';
    if (g_failed==0) {
        std::cout << "[PASS] M17-B2 durable fanout repository integration\n";
        std::cout << "[PASS] M17-B3 durable recipient replay repository integration\n";
    }
    return g_failed==0 ? 0 : 1;
}
