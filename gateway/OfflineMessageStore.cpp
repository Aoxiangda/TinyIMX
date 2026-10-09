#include "gateway/OfflineMessageStore.h"

#include "common/logging/LogMacros.h"

namespace tinyimx {

OfflineMessageStore::OfflineMessageStore(
    std::size_t max_messages_per_user
)
    : max_messages_per_user_(max_messages_per_user) {}

bool OfflineMessageStore::Store(UserId user_id, const Packet& packet) {
    if (user_id == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto& queue = messages_[user_id];

    if (queue.size() >= max_messages_per_user_) {
        queue.pop_front();

        LOG_WARN("offline message queue overflow, drop oldest"
                 << ", user_id=" << user_id
                 << ", max_messages_per_user="
                 << max_messages_per_user_);
    }

    queue.push_back(packet);

    LOG_INFO("offline message stored"
             << ", user_id=" << user_id
             << ", pending_count=" << queue.size());

    return true;
}

std::vector<Packet> OfflineMessageStore::PopAll(UserId user_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<Packet> result;

    auto iter = messages_.find(user_id);
    if (iter == messages_.end()) {
        return result;
    }

    auto& queue = iter->second;
    result.reserve(queue.size());

    while (!queue.empty()) {
        result.push_back(std::move(queue.front()));
        queue.pop_front();
    }

    messages_.erase(iter);

    LOG_INFO("offline messages popped"
             << ", user_id=" << user_id
             << ", count=" << result.size());

    return result;
}

std::size_t OfflineMessageStore::PendingCount(UserId user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto iter = messages_.find(user_id);
    if (iter == messages_.end()) {
        return 0;
    }

    return iter->second.size();
}

std::size_t OfflineMessageStore::TotalCount() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::size_t total = 0;

    for (const auto& item : messages_) {
        total += item.second.size();
    }

    return total;
}

std::size_t OfflineMessageStore::MaxMessagesPerUser() const {
    return max_messages_per_user_;
}

}  // namespace tinyimx