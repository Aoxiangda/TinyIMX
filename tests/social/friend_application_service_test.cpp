#include "services/social/application/FriendApplicationService.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakeFriendRepositoryPort final
    : public tinyimx::social::FriendRepositoryPort {
public:
    tinyimx::social::FriendRepositoryListResult ListFriends(
        std::uint64_t user_id,
        std::size_t limit
    ) override {
        ++call_count;
        last_user_id = user_id;
        last_limit = limit;

        auto result = next_result;
        return result;
    }

    std::size_t call_count{0};
    std::uint64_t last_user_id{0};
    std::size_t last_limit{0};
    tinyimx::social::FriendRepositoryListResult next_result;
};

bool Expect(bool condition, const char* name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return true;
    }

    std::cerr << "[FAIL] " << name << '\n';
    return false;
}

tinyimx::social::FriendView MakeFriend(
    std::uint64_t id
) {
    tinyimx::social::FriendView view;
    view.friend_user_id = id;
    view.username = "user" + std::to_string(id);
    view.nickname = "friend" + std::to_string(id);
    view.user_status = 1;
    view.relation_status = 1;
    return view;
}

bool TestValidationFastFail() {
    FakeFriendRepositoryPort repository;
    tinyimx::social::FriendApplicationService service(&repository);

    const auto zero_actor = service.ListFriends(0, 20);
    const auto zero_limit = service.ListFriends(10001, 0);
    const auto too_large = service.ListFriends(10001, 101);

    return Expect(
        zero_actor.status ==
            tinyimx::social::FriendApplicationStatus::kInvalidArgument &&
        zero_limit.status ==
            tinyimx::social::FriendApplicationStatus::kInvalidArgument &&
        too_large.status ==
            tinyimx::social::FriendApplicationStatus::kInvalidArgument &&
        repository.call_count == 0,
        "FriendApplication.ValidationFastFail"
    );
}

bool TestLimitPlusOneHasMore() {
    FakeFriendRepositoryPort repository;
    repository.next_result.status =
        tinyimx::social::FriendApplicationStatus::kSucceeded;

    for (std::uint64_t id = 1; id <= 101; ++id) {
        repository.next_result.records.push_back(
            MakeFriend(10000 + id)
        );
    }

    tinyimx::social::FriendApplicationService service(&repository);
    const auto result = service.ListFriends(10001, 100);

    return Expect(
        result.Succeeded() &&
        repository.call_count == 1 &&
        repository.last_user_id == 10001 &&
        repository.last_limit == 101 &&
        result.friends.size() == 100 &&
        result.has_more,
        "FriendApplication.LimitPlusOneHasMore"
    );
}

bool TestStorageFailurePropagation() {
    FakeFriendRepositoryPort repository;
    repository.next_result.status =
        tinyimx::social::FriendApplicationStatus::kStorageError;
    repository.next_result.message = "mysql unavailable";

    tinyimx::social::FriendApplicationService service(&repository);
    const auto result = service.ListFriends(10001, 20);

    return Expect(
        !result.Succeeded() &&
        result.status ==
            tinyimx::social::FriendApplicationStatus::kStorageError &&
        result.message == "mysql unavailable" &&
        repository.last_limit == 21,
        "FriendApplication.StorageFailurePropagation"
    );
}

}  // namespace

int main() {
    std::cout
        << "========== TinyIMX M14-A3 Friend Application Tests ==========\n";

    std::size_t failed = 0;
    failed += TestValidationFastFail() ? 0 : 1;
    failed += TestLimitPlusOneHasMore() ? 0 : 1;
    failed += TestStorageFailurePropagation() ? 0 : 1;

    std::cout
        << "=============================================================\n"
        << "total = 3, failed = " << failed << '\n';

    return failed == 0 ? 0 : 1;
}
