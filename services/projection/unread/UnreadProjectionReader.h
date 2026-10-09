#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace tinyimx {
class MySqlConnectionPool;
}

namespace tinyimx::projection::unread {

struct DialogUnreadSnapshot {
    std::uint64_t receiver_user_id{0};
    std::uint64_t peer_user_id{0};
    std::int64_t private_unread{0};
    std::int64_t total_unread{0};
};

struct UserUnreadSnapshot {
    std::uint64_t receiver_user_id{0};
    // Includes every historical peer for the receiver, including zero-unread
    // dialogs, so a cache rebuild can delete stale legacy private keys.
    std::vector<std::pair<std::uint64_t, std::int64_t>> peer_counts;
    std::int64_t total_unread{0};
};

template <typename T>
struct UnreadProjectionReadResult {
    bool success{false};
    T value;
    std::string message;
};

class UnreadProjectionReader final {
public:
    explicit UnreadProjectionReader(MySqlConnectionPool* pool);

    UnreadProjectionReader(const UnreadProjectionReader&) = delete;
    UnreadProjectionReader& operator=(const UnreadProjectionReader&) = delete;

    [[nodiscard]] UnreadProjectionReadResult<DialogUnreadSnapshot>
    LoadDialogSnapshot(
        std::uint64_t receiver_user_id,
        std::uint64_t peer_user_id
    );

    [[nodiscard]] UnreadProjectionReadResult<UserUnreadSnapshot>
    LoadUserSnapshot(std::uint64_t receiver_user_id);

private:
    MySqlConnectionPool* pool_{nullptr};  // non-owning
};

}  // namespace tinyimx::projection::unread
