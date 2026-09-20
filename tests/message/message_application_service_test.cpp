#include "services/message/application/MessageApplicationService.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

int g_failed = 0;

void Expect(bool condition, const char* label) {
    if (condition) {
        std::cout << "[PASS] " << label << '\n';
    } else {
        std::cout << "[FAIL] " << label << '\n';
        ++g_failed;
    }
}

class FakeRepository final : public tinyimx::message::MessageRepositoryPort {
public:

    tinyimx::message::MessageRepositoryPersistResult PersistPrivateMessage(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        const std::string& client_message_id,
        std::uint32_t message_type,
        const std::string& content
    ) override {
        tinyimx::message::MessageRepositoryPersistResult out;
        out.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
        out.outcome = persist_outcome;
        out.message_id = 9001;
        out.record = MakeMessage(9001, from_user_id, to_user_id,
                                 tinyimx::message::MessageDeliveryState::kPending);
        out.record.client_message_id = client_message_id;
        out.record.message_type = message_type;
        out.record.content = content;
        out.message = "persist fake";
        return out;
    }

    tinyimx::message::MessageRepositoryGroupGetResult
    FindGroupMessageByClientMessageId(
        std::uint64_t,
        const std::string&
    ) override {
        tinyimx::message::MessageRepositoryGroupGetResult out;
        out.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
        out.found = false;
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
        tinyimx::message::MessageRepositoryGroupPersistResult out;
        out.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
        out.outcome = tinyimx::message::PersistGroupMessageOutcome::kCreated;
        out.message_id = 9901;
        out.record.message_id = 9901;
        out.record.client_message_id = client_message_id;
        out.record.group_id = group_id;
        out.record.from_user_id = from_user_id;
        out.record.message_type = message_type;
        out.record.content = content;
        out.record.membership_epoch = membership_epoch;
        out.record.member_version = member_version;
        out.record.authorized_role = authorized_role;
        out.record.created_at = "2026-09-18 10:00:00";
        out.message = "fake group persist";
        return out;
    }

    tinyimx::message::MessageRepositoryGroupDeliveryGetResult GetGroupMessageDelivery(
        std::uint64_t, std::uint64_t) override {
        tinyimx::message::MessageRepositoryGroupDeliveryGetResult out;
        out.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
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
        std::uint64_t, const std::string&, const std::string&, std::size_t, std::uint32_t) override {
        tinyimx::message::MessageRepositoryGroupDeliveryListResult out;
        out.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
        return out;
    }
    tinyimx::message::MessageRepositoryMutationResult CompleteGroupMessageDeliveryAttempt(
        std::uint64_t, std::uint64_t, const std::string&,
        tinyimx::message::GroupDeliveryAttemptOutcome, const std::string&,
        std::uint32_t, const std::string&) override {
        return SuccessMutation(1);
    }
    tinyimx::message::MessageRepositoryMutationResult ConfirmGroupMessageDelivery(
        std::uint64_t, std::uint64_t) override { return SuccessMutation(1); }

    tinyimx::message::MessageRepositoryGetResult GetPrivateMessage(
        std::uint64_t message_id
    ) override {
        tinyimx::message::MessageRepositoryGetResult out;
        out.status = get_status;
        if (get_status != tinyimx::message::MessageApplicationStatus::kSucceeded) {
            out.message = "get failed";
            return out;
        }
        const auto it = messages.find(message_id);
        if (it == messages.end()) {
            out.found = false;
            return out;
        }
        out.found = true;
        out.record = it->second;
        return out;
    }

    tinyimx::message::MessageRepositoryHistoryResult ListHistory(
        std::uint64_t,
        std::uint64_t,
        std::uint64_t,
        std::size_t
    ) override {
        tinyimx::message::MessageRepositoryHistoryResult out;
        out.status = read_status;
        out.records = history_rows;
        return out;
    }

    tinyimx::message::MessageRepositoryConversationResult ListConversations(
        std::uint64_t,
        std::size_t
    ) override {
        tinyimx::message::MessageRepositoryConversationResult out;
        out.status = read_status;
        out.records = conversation_rows;
        return out;
    }

    tinyimx::message::MessageRepositoryCountResult CountPending(
        std::uint64_t
    ) override {
        tinyimx::message::MessageRepositoryCountResult out;
        out.status = read_status;
        out.count = pending_count;
        return out;
    }

    tinyimx::message::MessageRepositoryPendingResult ListPendingAfter(
        std::uint64_t,
        std::uint64_t,
        std::size_t
    ) override {
        tinyimx::message::MessageRepositoryPendingResult out;
        out.status = read_status;
        out.records = pending_rows;
        return out;
    }

    tinyimx::message::MessageRepositoryMutationResult ConfirmReceiver(
        std::uint64_t message_id
    ) override {
        ++confirm_calls;
        auto& row = messages.at(message_id);
        if (row.delivery_state == tinyimx::message::MessageDeliveryState::kPending) {
            row.delivery_state = tinyimx::message::MessageDeliveryState::kReceiverConfirmed;
            return SuccessMutation(1);
        }
        return SuccessMutation(0);
    }

    tinyimx::message::MessageRepositoryMutationResult ConfirmReceiverBatch(
        const std::vector<std::uint64_t>& message_ids
    ) override {
        ++batch_calls;
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
        std::uint64_t,
        std::uint64_t
    ) override {
        ++read_calls;
        return SuccessMutation(read_affected_rows);
    }

    static tinyimx::message::MessageView MakeMessage(
        std::uint64_t id,
        std::uint64_t from,
        std::uint64_t to,
        tinyimx::message::MessageDeliveryState state
    ) {
        tinyimx::message::MessageView row;
        row.message_id = id;
        row.client_message_id = "c" + std::to_string(id);
        row.from_user_id = from;
        row.to_user_id = to;
        row.message_type = 1;
        row.content = "{\"text\":\"hello\"}";
        row.delivery_state = state;
        row.created_at = "2026-09-04 10:00:00";
        return row;
    }

    static tinyimx::message::MessageRepositoryMutationResult SuccessMutation(
        std::uint64_t affected
    ) {
        tinyimx::message::MessageRepositoryMutationResult out;
        out.status = tinyimx::message::MessageApplicationStatus::kSucceeded;
        out.affected_rows = affected;
        return out;
    }

    tinyimx::message::PersistPrivateMessageOutcome persist_outcome{
        tinyimx::message::PersistPrivateMessageOutcome::kCreated};
    tinyimx::message::MessageApplicationStatus get_status{
        tinyimx::message::MessageApplicationStatus::kSucceeded};
    tinyimx::message::MessageApplicationStatus read_status{
        tinyimx::message::MessageApplicationStatus::kSucceeded};
    std::unordered_map<std::uint64_t, tinyimx::message::MessageView> messages;
    std::vector<tinyimx::message::MessageView> history_rows;
    std::vector<tinyimx::message::ConversationView> conversation_rows;
    std::vector<tinyimx::message::MessageView> pending_rows;
    std::uint64_t pending_count{0};
    std::uint64_t read_affected_rows{3};
    int confirm_calls{0};
    int batch_calls{0};
    int read_calls{0};
};

void TestValidationFastFail() {
    FakeRepository repository;
    tinyimx::message::MessageApplicationService app(&repository);
    Expect(!app.GetPrivateMessage(0).Succeeded() &&
               !app.CountPending(0).Succeeded() &&
               !app.ListPendingAfter(10002, 0, 0).Succeeded() &&
               !app.ConfirmReceiver(0, 10002).Succeeded() &&
               !app.ConfirmReceiverBatch(10002, {}).Succeeded() &&
               !app.MarkDialogRead(10002, 10002).Succeeded(),
           "C3ValidationFastFail");
}

void TestPersistCreatedReusedConflict() {
    FakeRepository repository;
    tinyimx::message::MessageApplicationService app(&repository);
    auto created = app.PersistPrivateMessage(10001, 10002, "c1", 1, "body");
    repository.persist_outcome = tinyimx::message::PersistPrivateMessageOutcome::kReused;
    auto reused = app.PersistPrivateMessage(10001, 10002, "c1", 1, "body");
    repository.persist_outcome = tinyimx::message::PersistPrivateMessageOutcome::kIdempotencyConflict;
    auto conflict = app.PersistPrivateMessage(10001, 10002, "c1", 1, "other");
    Expect(created.Created() && reused.Reused() && conflict.Conflict(),
           "PersistCreatedReusedConflict");
}

void TestGroupMessageApplicationFoundation() {
    FakeRepository repository;
    tinyimx::message::MessageApplicationService app(&repository);

    const auto invalid = app.PersistAuthorizedGroupMessage(
        0, 47, "m17b1-invalid", 1, "hello", 1, 4, 1
    );
    const auto created = app.PersistAuthorizedGroupMessage(
        10001, 47, "m17b1-app", 1, "hello", 2, 9, 3
    );
    const auto lookup = app.FindGroupMessageByClientMessageId(
        10001, "m17b1-missing"
    );

    Expect(
        invalid.status == tinyimx::message::MessageApplicationStatus::kInvalidArgument &&
        created.Created() && created.message_id == 9901 &&
        created.record.group_id == 47 &&
        created.record.membership_epoch == 2 &&
        created.record.member_version == 9 &&
        created.record.authorized_role == 3 &&
        lookup.Succeeded() && !lookup.Found(),
        "GroupMessageApplicationFoundation"
    );
}

void TestHistorySentinelPagination() {
    FakeRepository repository;
    for (std::uint64_t id = 1; id <= 3; ++id) {
        repository.history_rows.push_back(
            FakeRepository::MakeMessage(id, 10001, 10002,
                tinyimx::message::MessageDeliveryState::kPending));
    }
    tinyimx::message::MessageApplicationService app(&repository);
    auto result = app.ListHistory(10001, 10002, 0, 2);
    Expect(result.Succeeded() && result.has_more && result.messages.size() == 2 &&
               result.messages.front().message_id == 2,
           "HistorySentinelPagination");
}

void TestConversationSentinelPagination() {
    FakeRepository repository;
    for (std::uint64_t i = 0; i < 3; ++i) {
        tinyimx::message::ConversationView row;
        row.peer_user_id = 20000 + i;
        row.last_message_id = 100 - i;
        row.last_from_user_id = 10001;
        row.last_to_user_id = row.peer_user_id;
        row.last_message_type = 1;
        row.last_created_at = "2026-09-04 10:00:00";
        repository.conversation_rows.push_back(row);
    }
    tinyimx::message::MessageApplicationService app(&repository);
    auto result = app.ListConversations(10001, 2);
    Expect(result.Succeeded() && result.has_more && result.conversations.size() == 2,
           "ConversationSentinelPagination");
}

void TestGetPrivateMessage() {
    FakeRepository repository;
    repository.messages.emplace(
        77, FakeRepository::MakeMessage(
                77, 10001, 10002,
                tinyimx::message::MessageDeliveryState::kPending));
    tinyimx::message::MessageApplicationService app(&repository);
    const auto found = app.GetPrivateMessage(77);
    const auto missing = app.GetPrivateMessage(78);
    Expect(found.Succeeded() && found.record.message_id == 77 &&
               missing.status == tinyimx::message::MessageApplicationStatus::kNotFound,
           "GetPrivateMessage");
}

void TestPendingReadFoundation() {
    FakeRepository repository;
    repository.pending_count = 2;
    repository.pending_rows = {
        FakeRepository::MakeMessage(101, 10001, 10002,
            tinyimx::message::MessageDeliveryState::kPending),
        FakeRepository::MakeMessage(102, 10003, 10002,
            tinyimx::message::MessageDeliveryState::kPending),
    };
    tinyimx::message::MessageApplicationService app(&repository);
    const auto count = app.CountPending(10002);
    const auto page = app.ListPendingAfter(10002, 100, 100);
    Expect(count.Succeeded() && count.count == 2 && page.Succeeded() &&
               page.messages.size() == 2 && !page.has_more,
           "PendingReadFoundation");
}

void TestConfirmReceiverMonotonicOwnership() {
    FakeRepository repository;
    repository.messages.emplace(1, FakeRepository::MakeMessage(
        1, 10001, 10002, tinyimx::message::MessageDeliveryState::kPending));
    repository.messages.emplace(2, FakeRepository::MakeMessage(
        2, 10001, 10002, tinyimx::message::MessageDeliveryState::kRead));
    tinyimx::message::MessageApplicationService app(&repository);

    const auto wrong = app.ConfirmReceiver(1, 10003);
    const auto first = app.ConfirmReceiver(1, 10002);
    const auto duplicate = app.ConfirmReceiver(1, 10002);
    const auto read = app.ConfirmReceiver(2, 10002);
    Expect(wrong.status == tinyimx::message::MessageApplicationStatus::kPermissionDenied &&
               first.Succeeded() && first.affected_rows == 1 &&
               duplicate.Succeeded() && duplicate.affected_rows == 0 &&
               read.Succeeded() && read.affected_rows == 0 &&
               repository.messages.at(2).delivery_state ==
                   tinyimx::message::MessageDeliveryState::kRead,
           "ConfirmReceiverMonotonicOwnership");
}

void TestConfirmReceiverBatch() {
    FakeRepository repository;
    repository.messages.emplace(11, FakeRepository::MakeMessage(
        11, 10001, 10002, tinyimx::message::MessageDeliveryState::kPending));
    repository.messages.emplace(12, FakeRepository::MakeMessage(
        12, 10001, 10002, tinyimx::message::MessageDeliveryState::kReceiverConfirmed));
    tinyimx::message::MessageApplicationService app(&repository);
    const auto result = app.ConfirmReceiverBatch(10002, {11, 12});
    Expect(result.Succeeded() && result.affected_rows == 1 && repository.batch_calls == 1,
           "ConfirmReceiverBatch");
}

void TestMarkDialogRead() {
    FakeRepository repository;
    repository.read_affected_rows = 4;
    tinyimx::message::MessageApplicationService app(&repository);
    const auto result = app.MarkDialogRead(10002, 10001);
    Expect(result.Succeeded() && result.affected_rows == 4 && repository.read_calls == 1,
           "MarkDialogRead");
}

}  // namespace

int main() {
    std::cout << "========== TinyIMX M14-C3 Message Application Tests ==========\n";
    TestValidationFastFail();
    TestPersistCreatedReusedConflict();
    TestGroupMessageApplicationFoundation();
    TestHistorySentinelPagination();
    TestConversationSentinelPagination();
    TestGetPrivateMessage();
    TestPendingReadFoundation();
    TestConfirmReceiverMonotonicOwnership();
    TestConfirmReceiverBatch();
    TestMarkDialogRead();
    std::cout << "=============================================================\n";
    std::cout << "failed=" << g_failed << '\n';
    return g_failed == 0 ? 0 : 1;
}
