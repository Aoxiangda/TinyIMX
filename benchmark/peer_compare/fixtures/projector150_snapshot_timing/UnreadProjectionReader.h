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

class UnreadProjectionSource {
public:
    virtual ~UnreadProjectionSource() = default;
    [[nodiscard]] virtual UnreadProjectionReadResult<DialogUnreadSnapshot>
    LoadDialogSnapshot(std::uint64_t receiver_user_id, std::uint64_t peer_user_id) = 0;
    [[nodiscard]] virtual UnreadProjectionReadResult<UserUnreadSnapshot>
    LoadUserSnapshot(std::uint64_t receiver_user_id) = 0;
    // Individual outcomes are retained. Existing sources can use this fallback;
    // the MySQL reader overrides it with one bounded statement/read view.
    [[nodiscard]] virtual std::vector<UnreadProjectionReadResult<DialogUnreadSnapshot>>
    LoadDialogSnapshots(
        const std::vector<std::pair<std::uint64_t, std::uint64_t>>& dialogs
    ) {
        if (dialogs.size() > 256) return {};
        std::vector<UnreadProjectionReadResult<DialogUnreadSnapshot>> results;
        results.reserve(dialogs.size());
        for (const auto& [receiver, peer] : dialogs)
            results.push_back(LoadDialogSnapshot(receiver, peer));
        return results;
    }
};

class UnreadProjectionReader final : public UnreadProjectionSource {
public:
    explicit UnreadProjectionReader(MySqlConnectionPool* pool);

    UnreadProjectionReader(const UnreadProjectionReader&) = delete;
    UnreadProjectionReader& operator=(const UnreadProjectionReader&) = delete;

    [[nodiscard]] UnreadProjectionReadResult<DialogUnreadSnapshot>
    LoadDialogSnapshot(
        std::uint64_t receiver_user_id,
        std::uint64_t peer_user_id
    ) override;

    [[nodiscard]] UnreadProjectionReadResult<UserUnreadSnapshot>
    LoadUserSnapshot(std::uint64_t receiver_user_id) override;

    [[nodiscard]] std::vector<UnreadProjectionReadResult<DialogUnreadSnapshot>>
    LoadDialogSnapshots(
        const std::vector<std::pair<std::uint64_t, std::uint64_t>>& dialogs
    ) override;

private:
    MySqlConnectionPool* pool_{nullptr};  // non-owning
};

}  // namespace tinyimx::projection::unread
