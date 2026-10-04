#include "services/message/application/MessageApplicationService.h"
#include "services/message/server/MessageServiceServer.h"
#include "services/message/service/MessageServiceImpl.h"
#include "services/rpc/MessageRpcClient.h"
#include "services/rpc/GroupRpcClient.h"
#include "tinyimx/group/v1/group_service.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include "services/rpc/RpcCallOptions.h"
#include "services/rpc/StaticServiceEndpointProvider.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

class FakeMessageRepositoryPort final
    : public tinyimx::message::MessageRepositoryPort {
public:
    tinyimx::message::PendingRecipientsResult ListPendingRecipientsAfter(
        std::uint64_t after, std::size_t limit) override {
        tinyimx::message::PendingRecipientsResult out;
        out.status = discovery_status;
        if (!out.Succeeded()) return out;
        for (const auto id : {10002ULL, 10003ULL, 10009ULL})
            if (id > after) out.recipient_user_ids.push_back(id);
        out.has_more = out.recipient_user_ids.size() > limit;
        if (out.has_more) out.recipient_user_ids.resize(limit);
        return out;
    }
    tinyimx::message::MessageApplicationStatus discovery_status{
        tinyimx::message::MessageApplicationStatus::kSucceeded};
    tinyimx::message::MessageRepositoryGetResult FindPrivateMessageByClientMessageId(
        std::uint64_t sender, const std::string& cid
    ) override {
        ++resolve_calls;
        tinyimx::message::MessageRepositoryGetResult result;
        result.status = resolve_status;
        if (!result.Succeeded()) return result;
        for (const auto& [id, row] : messages) {
            (void)id;
            if (row.from_user_id == sender && row.client_message_id == cid) {
                result.found = true;
                result.record = row;
                break;
            }
        }
        return result;
    }

    std::size_t resolve_calls{0};
    tinyimx::message::MessageApplicationStatus resolve_status{
        tinyimx::message::MessageApplicationStatus::kSucceeded};
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

    tinyimx::message::MessageRepositoryGroupGetResult
    FindGroupMessageByClientMessageId(
        std::uint64_t from_user_id,
        const std::string& client_message_id
    ) override {
        ++group_lookup_calls;
        tinyimx::message::MessageRepositoryGroupGetResult out;
        out.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
        const std::string key =
            std::to_string(from_user_id) + ":" + client_message_id;
        const auto it = group_messages.find(key);
        if (it == group_messages.end()) {
            out.found = false;
            return out;
        }
        out.found = true;
        out.record = it->second;
        return out;
    }

    tinyimx::message::MessageRepositoryGroupPersistResult
    PersistAuthorizedGroupMessage(
        std::uint64_t from_user_id,
        std::uint64_t group_id,
        const std::string& client_message_id,
        std::uint32_t message_type,
        const std::string& content,
        std::uint64_t membership_epoch,
        std::uint64_t member_version,
        std::uint32_t authorized_role,
        const std::vector<std::uint64_t>& recipient_user_ids
    ) override {
        (void)recipient_user_ids;
        ++group_persist_calls;
        tinyimx::message::MessageRepositoryGroupPersistResult out;
        out.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
        const std::string key =
            std::to_string(from_user_id) + ":" + client_message_id;
        const auto existing = group_messages.find(key);
        if (existing != group_messages.end()) {
            out.message_id = existing->second.message_id;
            out.record = existing->second;
            const bool same =
                existing->second.group_id == group_id &&
                existing->second.message_type == message_type &&
                existing->second.content == content;
            out.outcome = same
                ? tinyimx::message::PersistGroupMessageOutcome::kReused
                : tinyimx::message::PersistGroupMessageOutcome::kIdempotencyConflict;
            out.message = same ? "fake group reused" : "fake group conflict";
            return out;
        }

        out.outcome = tinyimx::message::PersistGroupMessageOutcome::kCreated;
        out.message_id = next_group_message_id++;
        out.record.message_id = out.message_id;
        out.record.client_message_id = client_message_id;
        out.record.group_id = group_id;
        out.record.from_user_id = from_user_id;
        out.record.message_type = message_type;
        out.record.content = content;
        out.record.membership_epoch = membership_epoch;
        out.record.member_version = member_version;
        out.record.authorized_role = authorized_role;
        out.record.created_at = "2026-09-18 10:00:00";
        out.message = "fake group created";
        group_messages.emplace(key, out.record);
        return out;
    }

    tinyimx::message::MessageRepositoryGroupDeliveryGetResult GetGroupMessageDelivery(
        std::uint64_t message_id, std::uint64_t recipient_user_id) override {
        tinyimx::message::MessageRepositoryGroupDeliveryGetResult out;
        out.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
        for (const auto& [key, message] : group_messages) {
            (void)key;
            if (message.message_id == message_id) {
                out.found = true;
                out.record.message = message;
                out.record.delivery.message_id = message_id;
                out.record.delivery.group_id = message.group_id;
                out.record.delivery.recipient_user_id = recipient_user_id;
                out.record.delivery.delivery_state = tinyimx::message::GroupDeliveryState::kPending;
                return out;
            }
        }
        out.found = false;
        return out;
    }
    tinyimx::message::MessageRepositoryGroupDeliveryListResult ClaimGroupMessageDeliveries(
        const std::string&, const std::string&, std::size_t, std::uint32_t, std::uint64_t) override {
        tinyimx::message::MessageRepositoryGroupDeliveryListResult out;
        out.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
        return out;
    }
    tinyimx::message::MessageRepositoryGroupDeliveryListResult ClaimGroupMessageDeliveriesForRecipient(
        std::uint64_t recipient_user_id, const std::string& lease_owner,
        const std::string& lease_token, std::size_t, std::uint32_t) override {
        tinyimx::message::MessageRepositoryGroupDeliveryListResult out;
        out.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
        if (recipient_user_id != 0 && !lease_owner.empty() && !lease_token.empty()) {
            for (const auto& [key, message] : group_messages) {
                (void)key;
                tinyimx::message::GroupDeliveryWorkItem item;
                item.message = message;
                item.delivery.message_id = message.message_id;
                item.delivery.group_id = message.group_id;
                item.delivery.recipient_user_id = recipient_user_id;
                item.delivery.delivery_state = tinyimx::message::GroupDeliveryState::kDeferredOffline;
                item.delivery.lease_owner = lease_owner;
                item.delivery.lease_token = lease_token;
                out.records.push_back(std::move(item));
                break;
            }
        }
        return out;
    }
    tinyimx::message::MessageRepositoryMutationResult CompleteGroupMessageDeliveryAttempt(
        std::uint64_t, std::uint64_t, const std::string&,
        tinyimx::message::GroupDeliveryAttemptOutcome, const std::string&,
        std::uint32_t, const std::string&) override { return SuccessMutation(1); }
    tinyimx::message::MessageRepositoryMutationResult ConfirmGroupMessageDelivery(
        std::uint64_t, std::uint64_t) override { return SuccessMutation(1); }

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
    std::unordered_map<std::string, tinyimx::message::GroupMessageView> group_messages;
    std::uint64_t next_group_message_id{9901};
    std::size_t group_lookup_calls{0};
    std::size_t group_persist_calls{0};
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

class FakeGroupAuthorizationService final
    : public tinyimx::group::v1::GroupService::Service {
public:
    grpc::Status CheckGroupSendPermission(
        grpc::ServerContext*,
        const tinyimx::group::v1::CheckGroupSendPermissionRequest* request,
        tinyimx::group::v1::CheckGroupSendPermissionResponse* response
    ) override {
        ++calls;
        if (request == nullptr || response == nullptr ||
            request->actor_user_id() == 0 || request->group_id() == 0) {
            return {grpc::StatusCode::INVALID_ARGUMENT, "invalid group permission request"};
        }
        if (request->group_id() == 404) {
            return {grpc::StatusCode::NOT_FOUND, "group not found"};
        }
        if (request->group_id() == 48) {
            response->set_allowed(false);
            response->set_message("group is not active");
            return grpc::Status::OK;
        }
        if (!allowed) {
            response->set_allowed(false);
            response->set_message("user is not a group member");
            return grpc::Status::OK;
        }
        response->set_allowed(true);
        response->set_role(tinyimx::group::v1::GROUP_ROLE_MEMBER);
        response->set_membership_epoch(membership_epoch);
        response->set_member_version(member_version);
        response->set_message("group send allowed");
        return grpc::Status::OK;
    }

    grpc::Status PrepareGroupMessageSend(
        grpc::ServerContext*,
        const tinyimx::group::v1::PrepareGroupMessageSendRequest* request,
        tinyimx::group::v1::PrepareGroupMessageSendResponse* response
    ) override {
        ++calls;
        if (request == nullptr || response == nullptr ||
            request->actor_user_id() == 0 || request->group_id() == 0) {
            return {grpc::StatusCode::INVALID_ARGUMENT, "invalid group prepare request"};
        }
        if (request->group_id() == 404) {
            return {grpc::StatusCode::NOT_FOUND, "group not found"};
        }
        if (request->group_id() == 48) {
            response->set_allowed(false);
            response->set_message("group is not active");
            return grpc::Status::OK;
        }
        if (!allowed) {
            response->set_allowed(false);
            response->set_message("user is not a group member");
            return grpc::Status::OK;
        }
        response->set_allowed(true);
        response->set_role(tinyimx::group::v1::GROUP_ROLE_MEMBER);
        response->set_membership_epoch(membership_epoch);
        response->set_member_version(member_version);
        response->add_recipient_user_ids(10002);
        response->add_recipient_user_ids(10003);
        response->set_message("group send snapshot prepared");
        return grpc::Status::OK;
    }

    bool allowed{true};
    std::uint64_t membership_epoch{3};
    std::uint64_t member_version{9};
    std::size_t calls{0};
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

class ResolveResponseFixture final : public tinyimx::message::v1::MessageService::Service {
public:
    std::atomic<int> mode{0};
    std::atomic<int> discovery_mode{0};
    std::atomic<int> discovery_calls{0};
    grpc::Status ListPendingRecipientsAfter(
        grpc::ServerContext*,
        const tinyimx::message::v1::ListPendingRecipientsAfterRequest* request,
        tinyimx::message::v1::ListPendingRecipientsAfterResponse* response) override {
        ++discovery_calls;
        const auto behavior = discovery_mode.load();
        if (behavior == 7) return {grpc::StatusCode::UNIMPLEMENTED, "legacy service"};
        response->add_recipient_user_ids(request->after_user_id() + 1);
        response->add_recipient_user_ids(request->after_user_id() + 2);
        response->set_has_more(true);
        if (behavior == 1) response->set_recipient_user_ids(0, 0);
        if (behavior == 2) response->set_recipient_user_ids(0, request->after_user_id());
        if (behavior == 3) response->set_recipient_user_ids(1, response->recipient_user_ids(0));
        if (behavior == 4) response->set_recipient_user_ids(0, request->after_user_id() + 3);
        if (behavior == 5) response->add_recipient_user_ids(request->after_user_id() + 3);
        if (behavior == 6) response->clear_recipient_user_ids();
        return grpc::Status::OK;
    }
    grpc::Status ResolvePrivateMessage(
        grpc::ServerContext*,
        const tinyimx::message::v1::ResolvePrivateMessageRequest* request,
        tinyimx::message::v1::ResolvePrivateMessageResponse* response
    ) override {
        using namespace tinyimx::message::v1;
        const int behavior = mode.load();
        if (behavior == 13) return {grpc::StatusCode::UNIMPLEMENTED, "legacy service"};
        response->set_result(RESOLVE_PRIVATE_MESSAGE_RESULT_MATCHED_DURABLE);
        auto* record = response->mutable_record();
        record->set_message_id(77);
        record->set_from_user_id(request->from_user_id());
        record->set_to_user_id(request->to_user_id());
        record->set_client_message_id(request->client_message_id());
        record->set_message_type(request->message_type());
        record->set_content(request->content());
        record->set_delivery_state(MESSAGE_DELIVERY_STATE_PENDING);
        if (behavior == 1) response->set_result(RESOLVE_PRIVATE_MESSAGE_RESULT_UNSPECIFIED);
        if (behavior == 2) response->clear_record();
        if (behavior == 3) record->set_message_id(0);
        if (behavior == 4) record->set_from_user_id(10009);
        if (behavior == 5) record->set_to_user_id(10009);
        if (behavior == 6) record->set_client_message_id("other");
        if (behavior == 7) record->set_message_type(2);
        if (behavior == 8) record->set_content("other");
        if (behavior == 9) record->set_delivery_state(static_cast<MessageDeliveryState>(99));
        if (behavior == 10) response->set_result(RESOLVE_PRIVATE_MESSAGE_RESULT_NOT_OBSERVED);
        if (behavior == 11) response->set_result(RESOLVE_PRIVATE_MESSAGE_RESULT_IDEMPOTENCY_CONFLICT);
        if (behavior == 12) record->set_delivery_state(MESSAGE_DELIVERY_STATE_FAILED);
        return grpc::Status::OK;
    }
};

class DelayedDiscoveryEndpoint final : public tinyimx::rpc::ServiceEndpointProvider {
public:
    explicit DelayedDiscoveryEndpoint(std::string target) : target_(std::move(target)) {}
    std::optional<tinyimx::rpc::ServiceEndpoint> Resolve(tinyimx::rpc::ServiceKind) const override {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        return tinyimx::rpc::ServiceEndpoint{target_};
    }
private:
    std::string target_;
};

void TestResolveResponseValidation() {
    ResolveResponseFixture service;
    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    if (!server || port <= 0) {
        Expect(false, "Resolve.ResponseFixtureStart");
        return;
    }
    const auto provider = std::make_shared<tinyimx::rpc::StaticServiceEndpointProvider>(
        "", "", "127.0.0.1:" + std::to_string(port));
    tinyimx::rpc::MessageRpcClient client(provider);
    tinyimx::rpc::ResolvePrivateMessageRpcRequest request;
    request.from_user_id = 10001;
    request.to_user_id = 10002;
    request.client_message_id = "resolve-response-validation";
    request.message_type = 1;
    request.content = "body";
    tinyimx::rpc::RpcCallOptions options;
    options.remaining_timeout = std::chrono::seconds(1);
    const auto valid = client.ResolvePrivateMessage(request, options);
    Expect(valid.ok() && valid.value->record && valid.value->record->message_id == 77,
           "Resolve.ClientAcceptsVerifiedIdentity");
    for (int behavior = 1; behavior <= 11; ++behavior) {
        service.mode.store(behavior);
        const auto invalid = client.ResolvePrivateMessage(request, options);
        Expect(!invalid.ok() && invalid.status.code == tinyimx::rpc::RpcErrorCode::kDataLoss,
               "Resolve.ClientRejectsMalformedOutcomeOrIdentity");
    }
    service.mode.store(12);
    const auto failed = client.ResolvePrivateMessage(request, options);
    Expect(failed.ok() && failed.value->record && failed.value->record->delivery_state ==
               tinyimx::rpc::MessageDeliveryState::kFailed, "Resolve.ClientPreservesFailedDurableState");
    service.mode.store(13);
    const auto legacy = client.ResolvePrivateMessage(request, options);
    Expect(!legacy.ok() && legacy.status.code == tinyimx::rpc::RpcErrorCode::kUnimplemented,
           "Resolve.LegacyServiceIsErrorNotNotObserved");
    tinyimx::rpc::ListPendingRecipientsAfterRpcRequest discovery_request{100, 2};
    const auto discovery = client.ListPendingRecipientsAfter(discovery_request, options);
    Expect(discovery.ok() && discovery.value->recipient_user_ids == std::vector<std::uint64_t>{101, 102} &&
               discovery.value->has_more, "Discovery.ClientAcceptsStrictBoundedPage");
    for (int behavior = 1; behavior <= 6; ++behavior) {
        service.discovery_mode.store(behavior);
        const auto corrupt = client.ListPendingRecipientsAfter(discovery_request, options);
        Expect(!corrupt.ok() && corrupt.status.code == tinyimx::rpc::RpcErrorCode::kDataLoss,
               "Discovery.ClientRejectsMalformedPage");
    }
    service.discovery_mode.store(7);
    const auto old_discovery = client.ListPendingRecipientsAfter(discovery_request, options);
    Expect(!old_discovery.ok() && old_discovery.status.code == tinyimx::rpc::RpcErrorCode::kUnimplemented,
           "Discovery.LegacyEndpointRemainsError");
    service.discovery_mode.store(0);
    const auto calls_before_budget = service.discovery_calls.load();
    tinyimx::rpc::MessageRpcClient delayed(std::make_shared<DelayedDiscoveryEndpoint>(
        "127.0.0.1:" + std::to_string(port)));
    auto short_budget = options;
    short_budget.remaining_timeout = std::chrono::milliseconds(10);
    const auto exhausted = delayed.ListPendingRecipientsAfter(discovery_request, short_budget);
    Expect(!exhausted.ok() && exhausted.status.code == tinyimx::rpc::RpcErrorCode::kDeadlineExceeded &&
               service.discovery_calls.load() == calls_before_budget,
           "Discovery.EndpointResolutionCannotResetBudget");
    server->Shutdown();
    server->Wait();
}

}  // namespace

int main() {
    TestResolveResponseValidation();
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

    FakeGroupAuthorizationService group_authorization_service;
    grpc::ServerBuilder group_builder;
    int group_port = 0;
    group_builder.AddListeningPort(
        "127.0.0.1:0",
        grpc::InsecureServerCredentials(),
        &group_port
    );
    group_builder.RegisterService(&group_authorization_service);
    auto group_server = group_builder.BuildAndStart();
    if (!group_server || group_port <= 0) {
        Expect(false, "GroupService.AuthorizationServerStart");
        return 1;
    }

    const auto group_provider =
        std::make_shared<tinyimx::rpc::StaticServiceEndpointProvider>(
            "", "", "", "127.0.0.1:" + std::to_string(group_port));
    tinyimx::rpc::GroupRpcClient group_client(group_provider);

    tinyimx::message::MessageServiceImpl service_impl(
        &application,
        &group_client
    );
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

    tinyimx::rpc::PersistGroupMessageRpcRequest group_request;
    group_request.from_user_id = 10001;
    group_request.group_id = 47;
    group_request.client_message_id = "m17b1-grpc-group-1";
    group_request.message_type = 1;
    group_request.content = "hello-group";

    const auto group_created = client.PersistGroupMessage(group_request, options);
    Expect(
        group_created.ok() && group_created.attempted &&
        group_created.value->Created() &&
        group_created.value->record.membership_epoch == 3 &&
        group_created.value->record.member_version == 9 &&
        group_authorization_service.calls == 1,
        "MessageService.RealGrpcGroupPersistCreatedAuthorized"
    );

    // Durable truth must win over a later permission change. The retry must not
    // call GroupService again after the original group message is durable.
    group_authorization_service.allowed = false;
    const std::size_t auth_calls_before_retry = group_authorization_service.calls;
    const auto group_reused = client.PersistGroupMessage(group_request, options);
    Expect(
        group_reused.ok() && group_reused.value->Reused() &&
        group_reused.value->message_id == group_created.value->message_id &&
        group_authorization_service.calls == auth_calls_before_retry,
        "MessageService.GroupDurableTruthBeatsReauthorization"
    );


    tinyimx::rpc::ClaimGroupMessageDeliveriesForRecipientRpcRequest replay_claim;
    replay_claim.recipient_user_id = 10002;
    replay_claim.lease_owner = "gateway-replay-test";
    replay_claim.lease_token = "gateway-replay-test:lease-1";
    replay_claim.limit = 100;
    replay_claim.lease_ms = 10000;
    const auto replay_claim_result =
        client.ClaimGroupMessageDeliveriesForRecipient(replay_claim, options);
    Expect(
        replay_claim_result.ok() &&
        replay_claim_result.value->work_items.size() == 1 &&
        replay_claim_result.value->work_items.front().delivery.recipient_user_id == 10002 &&
        replay_claim_result.value->work_items.front().delivery.delivery_state ==
            tinyimx::rpc::GroupDeliveryRpcState::kDeferredOffline &&
        replay_claim_result.value->work_items.front().delivery.lease_token ==
            replay_claim.lease_token,
        "MessageService.RealGrpcClaimGroupDeliveriesForRecipient"
    );

    auto denied_group_request = group_request;
    denied_group_request.client_message_id = "m17b1-grpc-group-denied";
    const auto group_denied = client.PersistGroupMessage(denied_group_request, options);
    Expect(
        !group_denied.ok() && group_denied.attempted &&
        group_denied.status.code == tinyimx::rpc::RpcErrorCode::kPermissionDenied,
        "MessageService.RealGrpcGroupPermissionDenied"
    );

    auto inactive_group_request = group_request;
    inactive_group_request.group_id = 48;
    inactive_group_request.client_message_id = "m17b1-grpc-group-inactive";
    const auto group_inactive = client.PersistGroupMessage(inactive_group_request, options);
    Expect(
        !group_inactive.ok() && group_inactive.attempted &&
        group_inactive.status.code == tinyimx::rpc::RpcErrorCode::kFailedPrecondition,
        "MessageService.RealGrpcGroupInactive"
    );

    group_authorization_service.allowed = true;
    auto uncertain_group_request = group_request;
    uncertain_group_request.client_message_id = "m17b1-grpc-group-uncertain";
    ::setenv("TINYIMX_FAULT_GROUP_MESSAGE_PERSIST_POST_COMMIT_DELAY_MS", "200", 1);
    tinyimx::rpc::RpcCallOptions group_uncertain_options = options;
    group_uncertain_options.remaining_timeout = std::chrono::milliseconds(50);
    const auto group_uncertain =
        client.PersistGroupMessage(uncertain_group_request, group_uncertain_options);
    ::unsetenv("TINYIMX_FAULT_GROUP_MESSAGE_PERSIST_POST_COMMIT_DELAY_MS");
    Expect(
        !group_uncertain.ok() && group_uncertain.attempted,
        "MessageRpcClient.GroupPersistPostCommitTimeoutIsAttempted"
    );
    group_authorization_service.allowed = false;
    const std::size_t auth_calls_before_uncertain_retry =
        group_authorization_service.calls;
    const auto group_uncertain_retry =
        client.PersistGroupMessage(uncertain_group_request, options);
    Expect(
        group_uncertain_retry.ok() && group_uncertain_retry.value->Reused() &&
        group_authorization_service.calls == auth_calls_before_uncertain_retry,
        "MessageService.GroupPostCommitRetryReusesWithoutReauthorization"
    );

    tinyimx::rpc::GetPrivateMessageRpcRequest get_request;
    get_request.message_id = 7001;
    const auto get_result = client.GetPrivateMessage(get_request, options);
    Expect(get_result.ok() && get_result.value->record.message_id == 7001,
           "MessageService.RealGrpcGetPrivateMessage");
    get_request.message_id = 7999;
    const auto get_missing = client.GetPrivateMessage(get_request, options);
    Expect(!get_missing.ok() && get_missing.status.code == tinyimx::rpc::RpcErrorCode::kNotFound,
           "MessageService.RealGrpcGetPrivateMessageNotFound");

    tinyimx::rpc::ResolvePrivateMessageRpcRequest resolve_request;
    const auto& original = repository.messages.at(7001);
    resolve_request.from_user_id = original.from_user_id;
    resolve_request.to_user_id = original.to_user_id;
    resolve_request.client_message_id = original.client_message_id;
    resolve_request.message_type = original.message_type;
    resolve_request.content = original.content;
    const auto writes_before_resolve = repository.persist_calls;
    const auto matched = client.ResolvePrivateMessage(resolve_request, options);
    Expect(matched.ok() && matched.value->record && matched.value->record->message_id == 7001 &&
               matched.value->record->delivery_state == tinyimx::rpc::MessageDeliveryState::kPending,
           "Resolve.RealGrpcMatchedPending");
    auto missing_request = resolve_request;
    missing_request.client_message_id = "r2b1-late-commit";
    const auto not_observed = client.ResolvePrivateMessage(missing_request, options);
    Expect(not_observed.ok() && not_observed.value->outcome ==
               tinyimx::rpc::ResolvePrivateMessageRpcOutcome::kNotObserved && !not_observed.value->record,
           "Resolve.RealGrpcNotObserved");
    auto late = original;
    late.message_id = 7990;
    late.client_message_id = missing_request.client_message_id;
    repository.messages.emplace(late.message_id, late);
    const auto late_result = client.ResolvePrivateMessage(missing_request, options);
    Expect(late_result.ok() && late_result.value->record && late_result.value->record->message_id == 7990,
           "Resolve.RealGrpcLateCommitVisibleOnNextRead");
    repository.messages.erase(7990);
    auto resolve_conflict_request = resolve_request;
    resolve_conflict_request.content += "changed";
    const auto resolved_conflict = client.ResolvePrivateMessage(resolve_conflict_request, options);
    Expect(resolved_conflict.ok() && resolved_conflict.value->outcome ==
               tinyimx::rpc::ResolvePrivateMessageRpcOutcome::kIdempotencyConflict &&
               !resolved_conflict.value->record, "Resolve.RealGrpcConflictHasNoRecord");
    repository.resolve_status = tinyimx::message::MessageApplicationStatus::kStorageError;
    const auto read_error = client.ResolvePrivateMessage(resolve_request, options);
    Expect(!read_error.ok() && read_error.status.code == tinyimx::rpc::RpcErrorCode::kUnavailable,
           "Resolve.RealGrpcStorageFailureIsError");
    repository.resolve_status = tinyimx::message::MessageApplicationStatus::kSucceeded;
    const auto expired_resolve = client.ResolvePrivateMessage(resolve_request, tinyimx::rpc::RpcCallOptions{});
    Expect(!expired_resolve.ok() && expired_resolve.status.code == tinyimx::rpc::RpcErrorCode::kDeadlineExceeded,
           "Resolve.ZeroBudgetRejected");
    const auto missing_endpoint = empty_client.ResolvePrivateMessage(resolve_request, options);
    Expect(!missing_endpoint.ok() && missing_endpoint.status.code == tinyimx::rpc::RpcErrorCode::kUnavailable,
           "Resolve.MissingEndpointIsError");
    Expect(repository.persist_calls == writes_before_resolve && repository.messages.at(7001).delivery_state ==
               tinyimx::message::MessageDeliveryState::kPending, "Resolve.RealGrpcNoMutation");

    tinyimx::rpc::ListPendingRecipientsAfterRpcRequest discovery_request{0, 2};
    const auto recipients = client.ListPendingRecipientsAfter(discovery_request, options);
    Expect(recipients.ok() && recipients.value->recipient_user_ids ==
               std::vector<std::uint64_t>{10002, 10003} && recipients.value->has_more,
           "Discovery.RealGrpcFirstPage");
    discovery_request.after_user_id = 10003;
    const auto next_recipients = client.ListPendingRecipientsAfter(discovery_request, options);
    Expect(next_recipients.ok() && next_recipients.value->recipient_user_ids ==
               std::vector<std::uint64_t>{10009} && !next_recipients.value->has_more,
           "Discovery.RealGrpcCursorAndTerminalPage");
    repository.discovery_status = tinyimx::message::MessageApplicationStatus::kStorageError;
    const auto discovery_error = client.ListPendingRecipientsAfter(discovery_request, options);
    Expect(!discovery_error.ok() && discovery_error.status.code == tinyimx::rpc::RpcErrorCode::kUnavailable,
           "Discovery.RealGrpcStorageFailureIsError");
    repository.discovery_status = tinyimx::message::MessageApplicationStatus::kSucceeded;
    const auto expired_discovery = client.ListPendingRecipientsAfter(discovery_request, tinyimx::rpc::RpcCallOptions{});
    Expect(!expired_discovery.ok() && expired_discovery.status.code == tinyimx::rpc::RpcErrorCode::kDeadlineExceeded,
           "Discovery.ZeroBudgetRejected");
    const auto no_discovery_endpoint = empty_client.ListPendingRecipientsAfter(discovery_request, options);
    Expect(!no_discovery_endpoint.ok() && no_discovery_endpoint.status.code == tinyimx::rpc::RpcErrorCode::kUnavailable,
           "Discovery.MissingEndpointRejected");
    discovery_request.limit = 257;
    Expect(!client.ListPendingRecipientsAfter(discovery_request, options).ok(),
           "Discovery.ClientRejectsExcessPageLimit");
    Expect(repository.persist_calls == writes_before_resolve && repository.messages.at(7001).delivery_state ==
               tinyimx::message::MessageDeliveryState::kPending, "Discovery.RealGrpcNeverMutatesMessage");

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
    group_server->Shutdown();
    group_server->Wait();

    std::cout
        << "==================================================================\n"
        << "failed=" << g_failed << '\n';
    return g_failed == 0 ? 0 : 1;
}
