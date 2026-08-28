#include "gateway/ReceiverDeliveryTracker.h"

#include <algorithm>


namespace tinyimx {


ReceiverDeliveryTracker::
ReceiverDeliveryTracker(
    std::size_t max_recent_confirmed
)
    : max_recent_confirmed_(
          std::max<std::size_t>(
              max_recent_confirmed,
              1
          )
      ) {
}


ReceiverDeliveryRegisterStatus
ReceiverDeliveryTracker::
RegisterAttempt(
    std::uint64_t message_id,
    std::uint64_t receiver_user_id,
    std::uint32_t delivery_seq
) {
    if (
        message_id == 0 ||
        receiver_user_id == 0 ||
        delivery_seq == 0
    ) {
        return
            ReceiverDeliveryRegisterStatus::
                kInvalidArgument;
    }


    std::lock_guard<std::mutex>
        lock(mutex_);


    auto iterator =
        entries_.find(
            message_id
        );


    /*
     * 第一次看到该message。
     */
    if (
        iterator ==
        entries_.end()
    ) {
        Entry entry;

        entry.receiver_user_id =
            receiver_user_id;

        entry.current_delivery_seq =
            delivery_seq;

        entry.attempt_count = 1;

        entry.state =
            State::kWaitingAck;

        entry.attempt_seqs.insert(
            delivery_seq
        );


        entries_.emplace(
            message_id,
            std::move(entry)
        );


        return
            ReceiverDeliveryRegisterStatus::
                kRegistered;
    }


    Entry& entry =
        iterator->second;


    /*
     * server message_id一旦绑定Receiver，
     * 后续所有Retry都必须仍然属于同一个用户。
     */
    if (
        entry.receiver_user_id !=
        receiver_user_id
    ) {
        return
            ReceiverDeliveryRegisterStatus::
                kReceiverMismatch;
    }


    /*
     * 已经ReceiverConfirmed以后，
     * 不允许因为迟到的重试逻辑
     * 再把状态打开成WaitingAck。
     */
    if (
        entry.state ==
        State::kConfirmed
    ) {
        return
            ReceiverDeliveryRegisterStatus::
                kAlreadyConfirmed;
    }


    const auto [
        attempt_iterator,
        inserted
    ] =
        entry.attempt_seqs.insert(
            delivery_seq
        );


    (void)attempt_iterator;


    if (!inserted) {
        return
            ReceiverDeliveryRegisterStatus::
                kDuplicateAttempt;
    }


    entry.current_delivery_seq =
        delivery_seq;

    ++entry.attempt_count;


    return
        ReceiverDeliveryRegisterStatus::
            kRetryRegistered;
}

ReceiverDeliveryRetryRegisterStatus
ReceiverDeliveryTracker::
RegisterRetryAttempt(
    std::uint64_t message_id,
    std::uint64_t receiver_user_id,
    std::uint32_t
        expected_timed_out_delivery_seq,
    std::uint32_t new_delivery_seq,
    std::uint32_t max_attempts
) {
    if (
        message_id == 0 ||
        receiver_user_id == 0 ||
        expected_timed_out_delivery_seq == 0 ||
        new_delivery_seq == 0 ||
        max_attempts == 0
    ) {
        return
            ReceiverDeliveryRetryRegisterStatus::
                kInvalidArgument;
    }


    std::lock_guard<std::mutex>
        lock(mutex_);


    auto iterator =
        entries_.find(
            message_id
        );


    if (
        iterator ==
        entries_.end()
    ) {
        return
            ReceiverDeliveryRetryRegisterStatus::
                kUnknownMessage;
    }


    Entry& entry =
        iterator->second;


    /*
     * message_id一旦绑定Receiver，
     * Retry不能改变业务接收方。
     */
    if (
        entry.receiver_user_id !=
        receiver_user_id
    ) {
        return
            ReceiverDeliveryRetryRegisterStatus::
                kReceiverMismatch;
    }


    /*
     * Receiver已经确认M以后，
     * 任何旧timeout callback都必须停止。
     */
    if (
        entry.state ==
        State::kConfirmed
    ) {
        return
            ReceiverDeliveryRetryRegisterStatus::
                kAlreadyConfirmed;
    }


    /*
     * 最关键的stale timeout保护。
     *
     * 例如：
     *
     * current = D2
     *
     * 此时D1 timeout callback迟到，
     * 它不能再创建D3。
     */
    if (
        entry.current_delivery_seq !=
        expected_timed_out_delivery_seq
    ) {
        return
            ReceiverDeliveryRetryRegisterStatus::
                kStaleAttempt;
    }


    /*
     * attempt_count包含第一次Delivery。
     *
     * max_attempts=3：
     *
     * D1 count=1
     * D2 count=2
     * D3 count=3
     *
     * 然后停止。
     */
    if (
        entry.attempt_count >=
        max_attempts
    ) {
        return
            ReceiverDeliveryRetryRegisterStatus::
                kAttemptLimitReached;
    }


    const auto [
        attempt_iterator,
        inserted
    ] =
        entry.attempt_seqs.insert(
            new_delivery_seq
        );


    (void)attempt_iterator;


    if (!inserted) {
        return
            ReceiverDeliveryRetryRegisterStatus::
                kDuplicateAttempt;
    }


    /*
     * 到这里才真正把current Attempt
     * 从D_old推进到D_new。
     */
    entry.current_delivery_seq =
        new_delivery_seq;


    ++entry.attempt_count;


    return
        ReceiverDeliveryRetryRegisterStatus::
            kRetryRegistered;
}


ReceiverDeliveryAckStatus
ReceiverDeliveryTracker::
Acknowledge(
    std::uint64_t message_id,
    std::uint64_t receiver_user_id,
    std::uint32_t delivery_seq
) {
    if (
        message_id == 0 ||
        receiver_user_id == 0 ||
        delivery_seq == 0
    ) {
        return
            ReceiverDeliveryAckStatus::
                kInvalidArgument;
    }


    std::lock_guard<std::mutex>
        lock(mutex_);


    auto iterator =
        entries_.find(
            message_id
        );


    if (
        iterator ==
        entries_.end()
    ) {
        return
            ReceiverDeliveryAckStatus::
                kUnknownMessage;
    }


    Entry& entry =
        iterator->second;


    if (
        entry.receiver_user_id !=
        receiver_user_id
    ) {
        return
            ReceiverDeliveryAckStatus::
                kReceiverMismatch;
    }


    /*
     * ACK必须对应某个真实发出过的Attempt。
     *
     * 不能只凭message_id就确认。
     */
    if (
        entry.attempt_seqs.find(
            delivery_seq
        ) ==
        entry.attempt_seqs.end()
    ) {
        return
            ReceiverDeliveryAckStatus::
                kUnknownAttempt;
    }


    /*
     * 同一个合法Attempt重复ACK，
     * 或不同历史Attempt在消息已确认后迟到，
     * 都属于幂等Duplicate。
     */
    if (
        entry.state ==
        State::kConfirmed
    ) {
        return
            ReceiverDeliveryAckStatus::
                kDuplicate;
    }


    entry.state =
        State::kConfirmed;


    confirmed_order_.
        push_back(
            message_id
        );


    TrimConfirmedLocked();


    return
        ReceiverDeliveryAckStatus::
            kConfirmed;
}


bool
ReceiverDeliveryTracker::
GetSnapshot(
    std::uint64_t message_id,
    ReceiverDeliverySnapshot* snapshot
) const {
    if (
        message_id == 0 ||
        snapshot == nullptr
    ) {
        return false;
    }


    std::lock_guard<std::mutex>
        lock(mutex_);


    const auto iterator =
        entries_.find(
            message_id
        );


    if (
        iterator ==
        entries_.end()
    ) {
        return false;
    }


    const Entry& entry =
        iterator->second;


    ReceiverDeliverySnapshot result;

    result.message_id =
        message_id;

    result.receiver_user_id =
        entry.receiver_user_id;

    result.current_delivery_seq =
        entry.current_delivery_seq;

    result.attempt_count =
        entry.attempt_count;

    result.confirmed =
        entry.state ==
        State::kConfirmed;


    *snapshot =
        result;


    return true;
}


std::size_t
ReceiverDeliveryTracker::Size()
    const {
    std::lock_guard<std::mutex>
        lock(mutex_);

    return entries_.size();
}


std::size_t
ReceiverDeliveryTracker::
ConfirmedCount() const {
    std::lock_guard<std::mutex>
        lock(mutex_);

    return confirmed_order_.size();
}


void
ReceiverDeliveryTracker::
TrimConfirmedLocked() {
    while (
        confirmed_order_.size() >
        max_recent_confirmed_
    ) {
        const std::uint64_t
            oldest_message_id =
                confirmed_order_.front();


        confirmed_order_.pop_front();


        const auto iterator =
            entries_.find(
                oldest_message_id
            );


        if (
            iterator !=
                entries_.end() &&
            iterator->second.state ==
                State::kConfirmed
        ) {
            entries_.erase(
                iterator
            );
        }
    }
}


}  // namespace tinyimx