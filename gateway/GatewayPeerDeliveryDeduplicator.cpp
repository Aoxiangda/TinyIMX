#include "gateway/GatewayPeerDeliveryDeduplicator.h"

#include <algorithm>

namespace tinyimx {


GatewayPeerDeliveryDeduplicator::
GatewayPeerDeliveryDeduplicator(
    std::size_t max_recent_delivered
)
    : max_recent_delivered_(
          std::max<std::size_t>(
              max_recent_delivered,
              1
          )
      ) {
}


GatewayPeerDedupBeginStatus
GatewayPeerDeliveryDeduplicator::Begin(
    std::uint64_t message_id
) {
    if (message_id == 0) {
        return
            GatewayPeerDedupBeginStatus::
                kInvalidArgument;
    }


    /*
     * 从find到insert整个过程
     * 必须属于同一个临界区。
     */
    std::lock_guard<std::mutex>
        lock(mutex_);


    const auto iterator =
        states_.find(
            message_id
        );


    /*
     * 第一次看见。
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
            GatewayPeerDedupBeginStatus::
                kAcquired;
    }


    /*
     * 已经成功处理过。
     */
    if (
        iterator->second ==
        State::kDelivered
    ) {
        return
            GatewayPeerDedupBeginStatus::
                kAlreadyDelivered;
    }


    /*
     * 剩余情况只有Processing。
     */
    return
        GatewayPeerDedupBeginStatus::
            kAlreadyProcessing;
}


bool
GatewayPeerDeliveryDeduplicator::
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
     * 正常生命周期要求：
     *
     * Begin成功
     * ↓
     * 才允许MarkDelivered。
     */
    if (
        iterator ==
        states_.end()
    ) {
        return false;
    }


    /*
     * MarkDelivered自身也做幂等。
     *
     * 已经Delivered再次Mark，
     * 直接认为成功，
     * 不重复进入delivered_order_。
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
GatewayPeerDeliveryDeduplicator::Abort(
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
     * 清理目标已经满足。
     */
    if (
        iterator ==
        states_.end()
    ) {
        return true;
    }


    /*
     * 已经产生真实投递副作用的
     * Delivered不能被重新打开。
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
GatewayPeerDeliveryDeduplicator::Size()
    const {
    std::lock_guard<std::mutex>
        lock(mutex_);


    return states_.size();
}


std::size_t
GatewayPeerDeliveryDeduplicator::
DeliveredCount() const {
    std::lock_guard<std::mutex>
        lock(mutex_);


    return delivered_order_.size();
}


void
GatewayPeerDeliveryDeduplicator::
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