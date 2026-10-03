#include "services/social/application/FriendRequestApplicationService.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakeFriendRequestRepositoryPort final
    : public tinyimx::social::FriendRequestRepositoryPort {
public:
    tinyimx::social::CreateFriendRequestApplicationResult Create(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        const std::string& request_message
    ) override {
        ++create_calls;
        last_from_user_id = from_user_id;
        last_to_user_id = to_user_id;
        last_message = request_message;
        return create_result;
    }

    tinyimx::social::ListPendingFriendRequestsApplicationResult
    ListPendingIncoming(
        std::uint64_t receiver_user_id,
        const std::string& before_created_at,
        std::uint64_t before_request_id,
        std::size_t limit
    ) override {
        ++list_calls;
        last_receiver_user_id = receiver_user_id;
        last_before_created_at = before_created_at;
        last_before_request_id = before_request_id;
        last_limit = limit;
        return list_result;
    }

    tinyimx::social::AcceptFriendRequestApplicationResult Accept(
        std::uint64_t request_id,
        std::uint64_t handler_user_id
    ) override {
        ++accept_calls;
        last_request_id = request_id;
        last_handler_user_id = handler_user_id;
        return accept_result;
    }

    tinyimx::social::RejectFriendRequestApplicationResult Reject(
        std::uint64_t request_id,
        std::uint64_t handler_user_id
    ) override {
        ++reject_calls;
        last_request_id = request_id;
        last_handler_user_id = handler_user_id;
        return reject_result;
    }

    std::size_t create_calls{0};
    std::size_t list_calls{0};
    std::size_t accept_calls{0};
    std::size_t reject_calls{0};
    std::uint64_t last_from_user_id{0};
    std::uint64_t last_to_user_id{0};
    std::uint64_t last_receiver_user_id{0};
    std::uint64_t last_before_request_id{0};
    std::uint64_t last_request_id{0};
    std::uint64_t last_handler_user_id{0};
    std::size_t last_limit{0};
    std::string last_message;
    std::string last_before_created_at;

    tinyimx::social::CreateFriendRequestApplicationResult create_result;
    tinyimx::social::ListPendingFriendRequestsApplicationResult list_result;
    tinyimx::social::AcceptFriendRequestApplicationResult accept_result;
    tinyimx::social::RejectFriendRequestApplicationResult reject_result;
};

bool Expect(bool condition, const char* name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return true;
    }
    std::cerr << "[FAIL] " << name << '\n';
    return false;
}

bool TestValidationFastFail() {
    FakeFriendRequestRepositoryPort repository;
    tinyimx::social::FriendRequestApplicationService service(&repository);

    const auto bad_create = service.Create(0, 2, "hello");
    const auto self_create = service.Create(2, 2, "hello");
    const auto long_create = service.Create(1, 2, std::string(256, 'x'));
    const auto bad_accept = service.Accept(0, 2);
    const auto bad_reject = service.Reject(1, 0);
    const auto bad_list = service.ListPendingIncoming(0, "", 0, 20);
    const auto bad_cursor = service.ListPendingIncoming(2, "2026-09-29 12:00:00", 0, 20);

    return Expect(
        bad_create.outcome == tinyimx::social::FriendRequestCreateOutcome::kInvalidArgument &&
        self_create.outcome == tinyimx::social::FriendRequestCreateOutcome::kInvalidArgument &&
        long_create.outcome == tinyimx::social::FriendRequestCreateOutcome::kInvalidArgument &&
        bad_accept.outcome == tinyimx::social::FriendRequestAcceptOutcome::kInvalidArgument &&
        bad_reject.outcome == tinyimx::social::FriendRequestRejectOutcome::kInvalidArgument &&
        bad_list.outcome == tinyimx::social::FriendRequestListOutcome::kInvalidArgument &&
        bad_cursor.outcome == tinyimx::social::FriendRequestListOutcome::kInvalidCursor &&
        repository.create_calls == 0 && repository.accept_calls == 0 &&
        repository.reject_calls == 0 && repository.list_calls == 0,
        "FriendRequestApplication.ValidationFastFail"
    );
}

bool TestCreateOutcomePreserved() {
    FakeFriendRequestRepositoryPort repository;
    repository.create_result.outcome = tinyimx::social::FriendRequestCreateOutcome::kReopened;
    repository.create_result.request_id = 9001;
    repository.create_result.message = "reopened";
    tinyimx::social::FriendRequestApplicationService service(&repository);

    const auto result = service.Create(10001, 10002, "retry-safe");
    return Expect(
        result.Success() && result.Changed() && result.request_id == 9001 &&
        repository.create_calls == 1 && repository.last_from_user_id == 10001 &&
        repository.last_to_user_id == 10002 && repository.last_message == "retry-safe",
        "FriendRequestApplication.CreateOutcomePreserved"
    );
}

bool TestListLimitPlusOneAndCursor() {
    FakeFriendRequestRepositoryPort repository;
    repository.list_result.outcome = tinyimx::social::FriendRequestListOutcome::kSucceeded;
    for (std::uint64_t i = 1; i <= 4; ++i) {
        tinyimx::social::FriendRequestView view;
        view.request_id = 100 + i;
        view.from_user_id = 200 + i;
        view.to_user_id = 999;
        view.created_at = "2026-09-29 12:00:0" + std::to_string(i);
        repository.list_result.requests.push_back(std::move(view));
    }
    tinyimx::social::FriendRequestApplicationService service(&repository);

    const auto result = service.ListPendingIncoming(999, "", 0, 3);
    return Expect(
        result.Succeeded() && result.requests.size() == 3 && result.has_more &&
        result.next_before_request_id == 103 &&
        result.next_before_created_at == "2026-09-29 12:00:03" &&
        repository.last_limit == 4,
        "FriendRequestApplication.ListLimitPlusOneAndCursor"
    );
}

bool TestMutationIdempotentOutcomesPreserved() {
    FakeFriendRequestRepositoryPort repository;
    repository.accept_result.outcome = tinyimx::social::FriendRequestAcceptOutcome::kAlreadyAccepted;
    repository.accept_result.request_id = 77;
    repository.reject_result.outcome = tinyimx::social::FriendRequestRejectOutcome::kAlreadyRejected;
    repository.reject_result.request_id = 88;
    tinyimx::social::FriendRequestApplicationService service(&repository);

    const auto accept = service.Accept(77, 10002);
    const auto reject = service.Reject(88, 10003);
    return Expect(
        accept.Success() && !accept.Changed() &&
        reject.Success() && !reject.Changed() &&
        repository.accept_calls == 1 && repository.reject_calls == 1,
        "FriendRequestApplication.IdempotentOutcomesPreserved"
    );
}

}  // namespace

int main() {
    std::cout << "========== TinyIMX P0 Social Ownership Application Tests ==========\n";
    std::size_t failed = 0;
    failed += TestValidationFastFail() ? 0 : 1;
    failed += TestCreateOutcomePreserved() ? 0 : 1;
    failed += TestListLimitPlusOneAndCursor() ? 0 : 1;
    failed += TestMutationIdempotentOutcomesPreserved() ? 0 : 1;
    std::cout << "===================================================================\n"
              << "total = 4, failed = " << failed << '\n';
    return failed == 0 ? 0 : 1;
}
