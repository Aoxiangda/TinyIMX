#pragma once

#include "common/protocol/Packet.h"
#include "gateway/SessionManager.h"

#include <cstddef>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace tinyimx {

class OfflineMessageStore {
public:
    explicit OfflineMessageStore(
        std::size_t max_messages_per_user = 100
    );

    OfflineMessageStore(const OfflineMessageStore&) = delete;
    OfflineMessageStore& operator=(const OfflineMessageStore&) = delete;

    bool Store(UserId user_id, const Packet& packet);

    std::vector<Packet> PopAll(UserId user_id);

    std::size_t PendingCount(UserId user_id) const;
    std::size_t TotalCount() const;

    std::size_t MaxMessagesPerUser() const;

private:
    mutable std::mutex mutex_;

    std::size_t max_messages_per_user_{100};

    std::unordered_map<UserId, std::deque<Packet>> messages_;
};

}  // namespace tinyimx