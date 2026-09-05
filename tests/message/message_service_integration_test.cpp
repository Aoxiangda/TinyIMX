#include "services/message/application/MessageApplicationService.h"
#include "services/message/server/MessageServiceServer.h"
#include "services/message/service/MessageServiceImpl.h"
#include "services/rpc/MessageRpcClient.h"
#include "services/rpc/RpcCallOptions.h"
#include "services/rpc/StaticServiceEndpointProvider.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

class FakeMessageRepositoryPort final
    : public tinyimx::message::MessageRepositoryPort {
public:
    tinyimx::message::MessageRepositoryPersistResult PersistPrivateMessage(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        const std::string& client_message_id,
        std::uint32_t message_type,
        const std::string& content
    ) override {
        ++persist_calls;
        tinyimx::message::MessageRepositoryPersistResult result;
        result.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
        result.outcome = persist_outcome;
        result.message_id = 9001;
        result.record = MakeMessage(9001, from_user_id, to_user_id,
                                    tinyimx::message::MessageDeliveryState::kPending);
        result.record.client_message_id = client_message_id;
        result.record.message_type = message_type;
        result.record.content = content;
        if (persist_outcome ==
            tinyimx::message::PersistPrivateMessageOutcome::kIdempotencyConflict) {
            result.record.to_user_id = 10009;
        }
        result.message = "fake persist";
        return result;
    }

    tinyimx::message::MessageRepositoryGetResult GetPrivateMessage(
        std::uint64_t message_id
    ) override {
        ++get_calls;
        tinyimx::message::MessageRepositoryGetResult result;
        result.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
        const auto it = messages.find(message_id);
        if (it == messages.end()) {
            result.found = false;
            return result;
        }
        result.found = true;
        result.record = it->second;
        return result;
    }

    tinyimx::message::MessageRepositoryHistoryResult ListHistory(
        std::uint64_t actor_user_id,
        std::uint64_t peer_user_id,
        std::uint64_t before_message_id,
        std::size_t limit
    ) override {
        ++history_calls;
        last_actor = actor_user_id;
        last_peer = peer_user_id;
        last_before = before_message_id;
        last_limit = limit;

        tinyimx::message::MessageRepositoryHistoryResult result;
        result.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
        result.records = {
            MakeMessage(101, 10001, 10002, tinyimx::message::MessageDeliveryState::kPending),
            MakeMessage(102, 10001, 10002, tinyimx::message::MessageDeliveryState::kPending),
            MakeMessage(103, 10001, 10002, tinyimx::message::MessageDeliveryState::kPending),
        };
        return result;
    }

    tinyimx::message::MessageRepositoryConversationResult ListConversations(
        std::uint64_t actor_user_id,
        std::size_t limit
    ) override {
        ++conversation_calls;
        last_actor = actor_user_id;
        last_limit = limit;

        tinyimx::message::MessageRepositoryConversationResult result;
        result.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
        result.records = {
            MakeConversation(10002, 500),
            MakeConversation(10003, 400),
            MakeConversation(10004, 300),
        };
        return result;
    }

    tinyimx::message::MessageRepositoryCountResult CountPending(
        std::uint64_t
    ) override {
        ++count_calls;
        tinyimx::message::MessageRepositoryCountResult result;
        result.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
        result.count = pending_count;
        return result;
    }

    tinyimx::message::MessageRepositoryPendingResult ListPendingAfter(
        std::uint64_t to_user_id,
        std::uint64_t after_message_id,
        std::size_t limit
    ) override {
        ++pending_calls;
        last_limit = limit;
        tinyimx::message::MessageRepositoryPendingResult result;
        result.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
        for (const auto& [id, row] : messages) {
            if (id > after_message_id && row.to_user_id == to_user_id &&
                row.delivery_state == tinyimx::message::MessageDeliveryState::kPending) {
                result.records.push_back(row);
            }
        }
        std::sort(result.records.begin(), result.records.end(),
                  [](const auto& a, const auto& b) {
                      return a.message_id < b.message_id;
                  });
        if (result.records.size() > limit) {
            result.records.resize(limit);
        }
        return result;
    }

    tinyimx::message::MessageRepositoryMutationResult ConfirmReceiver(
        std::uint64_t message_id
    ) override {
        ++confirm_calls;
        auto& row = messages.at(message_id);
        std::uint64_t affected = 0;
        if (row.delivery_state == tinyimx::message::MessageDeliveryState::kPending) {
            row.delivery_state = tinyimx::message::MessageDeliveryState::kReceiverConfirmed;
            affected = 1;
        }
        return SuccessMutation(affected);
    }

    tinyimx::message::MessageRepositoryMutationResult ConfirmReceiverBatch(
        const std::vector<std::uint64_t>& message_ids
    ) override {
        ++confirm_batch_calls;
        std::uint64_t affected = 0;
        for (const auto id : message_ids) {
            auto& row = messages.at(id);
            if (row.delivery_state == tinyimx::message::MessageDeliveryState::kPending) {
                row.delivery_state = tinyimx::message::MessageDeliveryState::kReceiverConfirmed;
                ++affected;
            }
        }
        return SuccessMutation(affected);
    }

    tinyimx::message::MessageRepositoryMutationResult MarkDialogRead(
        std::uint64_t reader_user_id,
        std::uint64_t peer_user_id
    ) override {
        ++mark_read_calls;
        std::uint64_t affected = 0;
        for (auto& [id, row] : messages) {
            (void)id;
            if (row.to_user_id == reader_user_id &&
                row.from_user_id == peer_user_id &&
                row.delivery_state == tinyimx::message::MessageDeliveryState::kReceiverConfirmed) {
                row.delivery_state = tinyimx::message::MessageDeliveryState::kRead;
                ++affected;
            }
        }
        return SuccessMutation(affected);
    }

    static tinyimx::message::MessageView MakeMessage(
        std::uint64_t id,
        std::uint64_t from = 10001,
        std::uint64_t to = 10002,
        tinyimx::message::MessageDeliveryState state =
            tinyimx::message::MessageDeliveryState::kPending
    ) {
        tinyimx::message::MessageView view;
        view.message_id = id;
        view.client_message_id = "c-" + std::to_string(id);
        view.from_user_id = from;
        view.to_user_id = to;
        view.message_type = 1;
        view.content = "{\"text\":\"hello\"}";
        view.delivery_state = state;
        view.created_at = "2026-09-04 10:00:00";
        return view;
    }

    static tinyimx::message::ConversationView MakeConversation(
        std::uint64_t peer,
        std::uint64_t id
    ) {
        tinyimx::message::ConversationView view;
        view.peer_user_id = peer;
        view.last_message_id = id;
        view.last_client_message_id = "c-" + std::to_string(id);
        view.last_from_user_id = 10001;
        view.last_to_user_id = peer;
        view.last_message_type = 1;
        view.last_content = "{\"text\":\"last\"}";
        view.last_delivery_state = tinyimx::message::MessageDeliveryState::kPending;
        view.last_created_at = "2026-09-04 10:00:00";
        return view;
    }

    static tinyimx::message::MessageRepositoryMutationResult SuccessMutation(
        std::uint64_t affected
    ) {
        tinyimx::message::MessageRepositoryMutationResult result;
        result.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
        result.affected_rows = affected;
        return result;
    }

    tinyimx::message::PersistPrivateMessageOutcome persist_outcome{
        tinyimx::message::PersistPrivateMessageOutcome::kCreated};
    std::unordered_map<std::uint64_t, tinyimx::message::MessageView> messages;
    std::uint64_t pending_count{0};
    std::size_t persist_calls{0};
    std::size_t get_calls{0};
    std::size_t history_calls{0};
    std::size_t conversation_calls{0};
    std::size_t count_calls{0};
    std::size_t pending_calls{0};
    std::size_t confirm_calls{0};
    std::size_t confirm_batch_calls{0};
    std::size_t mark_read_calls{0};
    std::uint64_t last_actor{0};
    std::uint64_t last_peer{0};
    std::uint64_t last_before{0};
    std::size_t last_limit{0};
};

int g_failed = 0;

void Expect(bool condition, const char* name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return;
    }
    ++g_failed;
    std::cerr << "[FAIL] " << name << '\n';
}

}  // namespace

int main() {
    std::cout
        << "========== TinyIMX M14-C3 MessageService Integration Tests ==========\n";

    FakeMessageRepositoryPort repository;
    repository.messages.emplace(
        7001,
        FakeMessageRepositoryPort::MakeMessage(
            7001, 10001, 10002,
            tinyimx::message::MessageDeliveryState::kPending));
    repository.messages.emplace(
        7002,
        FakeMessageRepositoryPort::MakeMessage(
            7002, 10001, 10002,
            tinyimx::message::MessageDeliveryState::kPending));
    repository.messages.emplace(
        7003,
        FakeMessageRepositoryPort::MakeMessage(
            7003, 10001, 10002,
            tinyimx::message::MessageDeliveryState::kReceiverConfirmed));
    repository.pending_count = 2;

    tinyimx::message::MessageApplicationService application(&repository);
    tinyimx::message::MessageServiceImpl service_impl(&application);
    tinyimx::message::MessageServiceServer server(&service_impl);

    if (!server.Start("127.0.0.1:0")) {
        Expect(false, "MessageService.RealGrpcServerStart");
        return 1;
    }

    const auto provider =
        std::make_shared<tinyimx::rpc::StaticServiceEndpointProvider>(
            "", "", server.BoundTarget());
    tinyimx::rpc::MessageRpcClient client(provider);

    const auto empty_provider =
        std::make_shared<tinyimx::rpc::StaticServiceEndpointProvider>(
            "", "", "");
    tinyimx::rpc::MessageRpcClient empty_client(empty_provider);

    tinyimx::rpc::RpcCallOptions options;
    options.request_id = "m14-c3-request-1";
    options.trace_id = "m14-c3-trace-1";
    options.caller_service = "gateway-test";
    options.caller_instance = "gateway-test-a";
    options.remaining_timeout = std::chrono::seconds(1);

    tinyimx::rpc::PersistPrivateMessageRpcRequest persist_request;
    persist_request.from_user_id = 10001;
    persist_request.to_user_id = 10002;
    persist_request.client_message_id = "c3-grpc-persist-1";
    persist_request.message_type = 1;
    persist_request.content = "{\"from\":10001,\"to\":10002,\"text\":\"hello\"}";

    const auto persist_no_endpoint =
        empty_client.PersistPrivateMessage(persist_request, options);
    Expect(!persist_no_endpoint.ok() && !persist_no_endpoint.attempted &&
               persist_no_endpoint.status.code == tinyimx::rpc::RpcErrorCode::kUnavailable,
           "MessageRpcClient.PersistMissingEndpointNotAttempted");

    const auto persist_created = client.PersistPrivateMessage(persist_request, options);
    Expect(persist_created.ok() && persist_created.attempted &&
               persist_created.value->Created() && persist_created.value->message_id == 9001,
           "MessageService.RealGrpcPersistCreated");

    tinyimx::rpc::RpcCallOptions expired_options = options;
    expired_options.remaining_timeout = std::chrono::milliseconds::zero();
    const auto persist_not_attempted =
        client.PersistPrivateMessage(persist_request, expired_options);
    Expect(!persist_not_attempted.ok() && !persist_not_attempted.attempted &&
               persist_not_attempted.status.code == tinyimx::rpc::RpcErrorCode::kDeadlineExceeded,
           "MessageRpcClient.PersistExpiredBudgetNotAttempted");

    ::setenv("TINYIMX_FAULT_MESSAGE_PERSIST_POST_COMMIT_DELAY_MS", "200", 1);
    tinyimx::rpc::RpcCallOptions uncertain_options = options;
    uncertain_options.remaining_timeout = std::chrono::milliseconds(50);
    auto uncertain_request = persist_request;
    uncertain_request.client_message_id = "c3-grpc-persist-uncertain";
    const auto persist_uncertain =
        client.PersistPrivateMessage(uncertain_request, uncertain_options);
    ::unsetenv("TINYIMX_FAULT_MESSAGE_PERSIST_POST_COMMIT_DELAY_MS");
    Expect(!persist_uncertain.ok() && persist_uncertain.attempted,
           "MessageRpcClient.PersistPostCommitTimeoutIsAttempted");

    repository.persist_outcome = tinyimx::message::PersistPrivateMessageOutcome::kReused;
    const auto persist_reused = client.PersistPrivateMessage(persist_request, options);
    Expect(persist_reused.ok() && persist_reused.value->Reused(),
           "MessageService.RealGrpcPersistReused");

    repository.persist_outcome =
        tinyimx::message::PersistPrivateMessageOutcome::kIdempotencyConflict;
    auto conflict_request = persist_request;
    conflict_request.to_user_id = 10003;
    const auto persist_conflict = client.PersistPrivateMessage(conflict_request, options);
    Expect(persist_conflict.ok() && persist_conflict.value->Conflict(),
           "MessageService.RealGrpcPersistConflict");

    tinyimx::rpc::GetPrivateMessageRpcRequest get_request;
    get_request.message_id = 7001;
    const auto get_result = client.GetPrivateMessage(get_request, options);
    Expect(get_result.ok() && get_result.value->record.message_id == 7001,
           "MessageService.RealGrpcGetPrivateMessage");
    get_request.message_id = 7999;
    const auto get_missing = client.GetPrivateMessage(get_request, options);
    Expect(!get_missing.ok() && get_missing.status.code == tinyimx::rpc::RpcErrorCode::kNotFound,
           "MessageService.RealGrpcGetPrivateMessageNotFound");

    tinyimx::rpc::CountPendingRpcRequest count_request;
    count_request.to_user_id = 10002;
    const auto count_result = client.CountPending(count_request, options);
    Expect(count_result.ok() && count_result.value->count == 2,
           "MessageService.RealGrpcCountPending");

    tinyimx::rpc::ListPendingAfterRpcRequest pending_request;
    pending_request.to_user_id = 10002;
    pending_request.after_message_id = 0;
    pending_request.limit = 100;
    const auto pending_result = client.ListPendingAfter(pending_request, options);
    Expect(pending_result.ok() && pending_result.value->messages.size() == 2 &&
               pending_result.value->messages[0].message_id == 7001 &&
               pending_result.value->messages[1].message_id == 7002,
           "MessageService.RealGrpcListPendingAfter");

    tinyimx::rpc::ConfirmReceiverRpcRequest wrong_confirm;
    wrong_confirm.message_id = 7001;
    wrong_confirm.receiver_user_id = 10003;
    const auto wrong_confirm_result = client.ConfirmReceiver(wrong_confirm, options);
    Expect(!wrong_confirm_result.ok() && wrong_confirm_result.attempted &&
               wrong_confirm_result.status.code == tinyimx::rpc::RpcErrorCode::kPermissionDenied,
           "MessageService.RealGrpcConfirmWrongReceiver");

    tinyimx::rpc::ConfirmReceiverRpcRequest confirm;
    confirm.message_id = 7001;
    confirm.receiver_user_id = 10002;
    const auto confirm_result = client.ConfirmReceiver(confirm, options);
    const auto confirm_duplicate = client.ConfirmReceiver(confirm, options);
    Expect(confirm_result.ok() && confirm_result.value->affected_rows == 1 &&
               confirm_duplicate.ok() && confirm_duplicate.value->affected_rows == 0,
           "MessageService.RealGrpcConfirmReceiverIdempotent");

    repository.messages.at(7002).delivery_state =
        tinyimx::message::MessageDeliveryState::kPending;
    ::setenv("TINYIMX_FAULT_MESSAGE_CONFIRM_POST_COMMIT_DELAY_MS", "200", 1);
    tinyimx::rpc::RpcCallOptions confirm_uncertain_options = options;
    confirm_uncertain_options.remaining_timeout = std::chrono::milliseconds(50);
    tinyimx::rpc::ConfirmReceiverRpcRequest uncertain_confirm;
    uncertain_confirm.message_id = 7002;
    uncertain_confirm.receiver_user_id = 10002;
    const auto uncertain_confirm_result =
        client.ConfirmReceiver(uncertain_confirm, confirm_uncertain_options);
    ::unsetenv("TINYIMX_FAULT_MESSAGE_CONFIRM_POST_COMMIT_DELAY_MS");
    Expect(!uncertain_confirm_result.ok() && uncertain_confirm_result.attempted &&
               repository.messages.at(7002).delivery_state ==
                   tinyimx::message::MessageDeliveryState::kReceiverConfirmed,
           "MessageRpcClient.ConfirmPostCommitTimeoutIsAttempted");

    repository.messages.at(7002).delivery_state =
        tinyimx::message::MessageDeliveryState::kPending;
    tinyimx::rpc::ConfirmReceiverBatchRpcRequest batch_request;
    batch_request.receiver_user_id = 10002;
    batch_request.message_ids = {7002, 7003};
    const auto batch_result = client.ConfirmReceiverBatch(batch_request, options);
    Expect(batch_result.ok() && batch_result.value->affected_rows == 1,
           "MessageService.RealGrpcConfirmReceiverBatch");

    repository.messages.at(7001).delivery_state =
        tinyimx::message::MessageDeliveryState::kReceiverConfirmed;
    ::setenv("TINYIMX_FAULT_MESSAGE_MARK_READ_POST_COMMIT_DELAY_MS", "200", 1);
    tinyimx::rpc::RpcCallOptions read_uncertain_options = options;
    read_uncertain_options.remaining_timeout = std::chrono::milliseconds(50);
    tinyimx::rpc::MarkDialogReadRpcRequest read_request;
    read_request.reader_user_id = 10002;
    read_request.peer_user_id = 10001;
    const auto read_uncertain = client.MarkDialogRead(read_request, read_uncertain_options);
    ::unsetenv("TINYIMX_FAULT_MESSAGE_MARK_READ_POST_COMMIT_DELAY_MS");
    Expect(!read_uncertain.ok() && read_uncertain.attempted &&
               repository.messages.at(7001).delivery_state ==
                   tinyimx::message::MessageDeliveryState::kRead,
           "MessageRpcClient.MarkReadPostCommitTimeoutIsAttempted");

    const auto read_retry = client.MarkDialogRead(read_request, options);
    Expect(read_retry.ok() && read_retry.value->affected_rows == 0,
           "MessageService.RealGrpcMarkReadIdempotentRetry");

    tinyimx::rpc::ListHistoryRpcRequest history_request;
    history_request.actor_user_id = 10001;
    history_request.peer_user_id = 10002;
    history_request.before_message_id = 0;
    history_request.limit = 2;
    const auto history_result = client.ListHistory(history_request, options);
    Expect(history_result.ok() && history_result.value->has_more &&
               history_result.value->messages.size() == 2,
           "MessageService.RealGrpcListHistory");

    tinyimx::rpc::ListConversationsRpcRequest conversation_request;
    conversation_request.actor_user_id = 10001;
    conversation_request.limit = 2;
    const auto conversation_result = client.ListConversations(conversation_request, options);
    Expect(conversation_result.ok() && conversation_result.value->has_more &&
               conversation_result.value->conversations.size() == 2,
           "MessageService.RealGrpcListConversations");

    tinyimx::rpc::ListHistoryRpcRequest invalid_request;
    invalid_request.actor_user_id = 10001;
    invalid_request.peer_user_id = 10001;
    invalid_request.limit = 20;
    const auto invalid_result = client.ListHistory(invalid_request, options);
    Expect(!invalid_result.ok() &&
               invalid_result.status.code == tinyimx::rpc::RpcErrorCode::kInvalidArgument,
           "MessageRpcClient.ValidationFastFail");

    server.Shutdown();
    server.Wait();

    std::cout
        << "==================================================================\n"
        << "failed=" << g_failed << '\n';
    return g_failed == 0 ? 0 : 1;
}
