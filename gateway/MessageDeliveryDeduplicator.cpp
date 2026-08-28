#include "gateway/MessageDeliveryDeduplicator.h"

#include <algorithm>


namespace tinyimx {


MessageDeliveryDeduplicator::
MessageDeliveryDeduplicator(
    std::size_t max_recent_delivered
)
    : max_recent_delivered_(
          std::max<std::size_t>(
              max_recent_delivered,
              1
          )
      ) {
}


MessageDeliveryDedupBeginStatus
MessageDeliveryDeduplicator::Begin(
    std::uint64_t message_id
) {
    if (message_id == 0) {
        return
            MessageDeliveryDedupBeginStatus::
                kInvalidArgument;
    }


    /*
     * find + insert必须属于同一个临界区。
     *
     * 否则：
     *
     * Thread A find miss
     * Thread B find miss
     *
     * A认为自己是Owner
     * B也认为自己是Owner
     *
     * 就可能产生两次SendPacket副作用。
     */
    std::lock_guard<std::mutex>
        lock(mutex_);


    const auto iterator =
        states_.find(
            message_id
        );


    /*
     * 第一次看到该server message。
     *
     * 当前调用者成为唯一Owner。
     */
    if (
        iterator ==
        states_.end()
    ) {
        states_.emplace(
            message_id,
            State::kProcessing
        );


        return
            MessageDeliveryDedupBeginStatus::
                kAcquired;
    }


    /*
     * 已经完成过真实投递副作用。
     */
    if (
        iterator->second ==
        State::kDelivered
    ) {
        return
            MessageDeliveryDedupBeginStatus::
                kAlreadyDelivered;
    }


    /*
     * 剩余唯一状态：
     *
     * Processing。
     *
     * 表示另一执行者已经拥有Owner权限。
     */
    return
        MessageDeliveryDedupBeginStatus::
            kAlreadyProcessing;
}


bool
MessageDeliveryDeduplicator::
MarkDelivered(
    std::uint64_t message_id
) {
    if (message_id == 0) {
        return false;
    }


    std::lock_guard<std::mutex>
        lock(mutex_);


    const auto iterator =
        states_.find(
            message_id
        );


    /*
     * 正常状态机要求：
     *
     * Begin(kAcquired)
     * ↓
     * MarkDelivered
     */
    if (
        iterator ==
        states_.end()
    ) {
        return false;
    }


    /*
     * MarkDelivered自身保持幂等。
     */
    if (
        iterator->second ==
        State::kDelivered
    ) {
        return true;
    }


    iterator->second =
        State::kDelivered;


    delivered_order_.
        push_back(
            message_id
        );


    TrimDeliveredLocked();


    return true;
}


bool
MessageDeliveryDeduplicator::Abort(
    std::uint64_t message_id
) {
    if (message_id == 0) {
        return false;
    }


    std::lock_guard<std::mutex>
        lock(mutex_);


    const auto iterator =
        states_.find(
            message_id
        );


    /*
     * 已经不存在：
     *
     * 目标状态本来就是NotSeen，
     * 因此认为Abort成功。
     */
    if (
        iterator ==
        states_.end()
    ) {
        return true;
    }


    /*
     * 一旦已经Delivered，
     * 就不能重新打开执行权。
     *
     * 因为真实Receiver副作用
     * 可能已经发生。
     */
    if (
        iterator->second ==
        State::kDelivered
    ) {
        return false;
    }


    states_.erase(
        iterator
    );


    return true;
}


std::size_t
MessageDeliveryDeduplicator::Size()
    const {
    std::lock_guard<std::mutex>
        lock(mutex_);


    return states_.size();
}


std::size_t
MessageDeliveryDeduplicator::
DeliveredCount() const {
    std::lock_guard<std::mutex>
        lock(mutex_);


    return delivered_order_.size();
}


void
MessageDeliveryDeduplicator::
TrimDeliveredLocked() {
    while (
        delivered_order_.size() >
        max_recent_delivered_
    ) {
        const std::uint64_t
            oldest_message_id =
                delivered_order_.front();


        delivered_order_.pop_front();


        const auto iterator =
            states_.find(
                oldest_message_id
            );


        if (
            iterator !=
                states_.end() &&
            iterator->second ==
                State::kDelivered
        ) {
            states_.erase(
                iterator
            );
        }
    }
}


}  // namespace tinyimx