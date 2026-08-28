#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>


namespace tinyimx {


enum class ReceiverDeliveryRegisterStatus {
    /*
     * 第一次为该message_id登记
     * Receiver Delivery Attempt。
     */
    kRegistered = 0,

    /*
     * 同一个message_id登记了新的Retry Attempt。
     */
    kRetryRegistered,

    /*
     * 同一个delivery_seq已经登记过。
     *
     * 不增加attempt_count。
     */
    kDuplicateAttempt,

    /*
     * 该message_id已经被Receiver确认。
     *
     * 不允许重新打开WaitingAck。
     */
    kAlreadyConfirmed,

    /*
     * 同一个message_id却尝试绑定不同Receiver。
     *
     * 属于业务身份冲突。
     */
    kReceiverMismatch,

    kInvalidArgument
};


enum class ReceiverDeliveryAckStatus {
    /*
     * 本次ACK第一次成功确认消息。
     */
    kConfirmed = 0,

    /*
     * 消息已经确认过。
     *
     * 本次是合法重复ACK。
     */
    kDuplicate,

    /*
     * Gateway当前Tracker没有该message_id。
     */
    kUnknownMessage,

    /*
     * ACK连接所属用户不是该消息Receiver。
     */
    kReceiverMismatch,

    /*
     * Packet.seq从未作为该message_id
     * 的Delivery Attempt发出。
     */
    kUnknownAttempt,

    kInvalidArgument
};


enum class ReceiverDeliveryRetryRegisterStatus {
    /*
     * timed_out_delivery_seq仍然是当前Attempt，
     * 并成功登记了新的Retry Attempt。
     */
    kRetryRegistered = 0,

    /*
     * 该message已经被Receiver ACK确认。
     *
     * 不能再产生新Retry。
     */
    kAlreadyConfirmed,

    /*
     * timeout对应的Attempt已经不是当前Attempt。
     *
     * 典型情况：
     *
     * D1 timeout
     *     ↓
     * 已经产生D2
     *     ↓
     * D1的旧timeout callback又迟到
     *
     * 必须忽略。
     */
    kStaleAttempt,

    /*
     * 已经达到配置的最大Delivery Attempt数。
     *
     * max_attempts包含第一次发送。
     *
     * 例如max_attempts=3：
     *
     * D1
     * D2
     * D3
     *
     * 不再产生D4。
     */
    kAttemptLimitReached,

    /*
     * message_id存在，
     * 但Receiver身份与最初绑定身份不一致。
     */
    kReceiverMismatch,

    /*
     * new_delivery_seq以前已经被该message使用过。
     */
    kDuplicateAttempt,

    /*
     * Tracker没有该message。
     */
    kUnknownMessage,

    kInvalidArgument
};


struct ReceiverDeliverySnapshot {
    std::uint64_t message_id{0};

    std::uint64_t receiver_user_id{0};

    /*
     * 最新一次Delivery Attempt seq。
     */
    std::uint32_t current_delivery_seq{0};

    /*
     * 历史上一共登记过多少个不同Attempt。
     */
    std::uint32_t attempt_count{0};

    bool confirmed{false};
};


class ReceiverDeliveryTracker {
public:
    explicit ReceiverDeliveryTracker(
        std::size_t max_recent_confirmed =
            10000
    );


    ReceiverDeliveryTracker(
        const ReceiverDeliveryTracker&
    ) = delete;


    ReceiverDeliveryTracker&
    operator=(
        const ReceiverDeliveryTracker&
    ) = delete;


    /*
     * 登记一次真实准备发送给Receiver的Attempt。
     *
     * 同一个message_id允许：
     *
     * D1
     * D2
     * D3
     *
     * 但receiver_user_id必须始终一致。
     */
    ReceiverDeliveryRegisterStatus
    RegisterAttempt(
        std::uint64_t message_id,
        std::uint64_t receiver_user_id,
        std::uint32_t delivery_seq
    );

    /*
    * 在某次Receiver ACK Timeout以后，
    * 原子地把：
    *
    *     M / D_old
    *
    * 推进为：
    *
    *     M / D_new
    *
    * expected_timed_out_delivery_seq：
    *     触发本次timeout的Attempt。
    *
    * new_delivery_seq：
    *     本次Retry的新Attempt seq。
    *
    * max_attempts：
    *     包含第一次发送在内的最大Attempt总数。
    *
    * 该函数必须在同一把mutex下同时完成：
    *
    * 1. message是否存在
    * 2. Receiver是否一致
    * 3. 是否已经Confirmed
    * 4. timeout是否仍针对current Attempt
    * 5. 是否达到Retry上限
    * 6. new seq是否重复
    * 7. current seq推进
    *
    * 避免两个并发timeout同时制造两个Retry。
    */
    ReceiverDeliveryRetryRegisterStatus
        RegisterRetryAttempt(
            std::uint64_t message_id,
            std::uint64_t receiver_user_id,
            std::uint32_t expected_timed_out_delivery_seq,
            std::uint32_t new_delivery_seq,
            std::uint32_t max_attempts
        );
    /*
     * 处理Receiver ACK。
     *
     * message_id:
     *     稳定业务身份。
     *
     * delivery_seq:
     *     某次真实Delivery Attempt身份。
     *
     * 旧Attempt迟到ACK仍然可以确认消息：
     *
     * D1 timeout
     * D2 sent
     * ACK(D1) late
     *
     * 只要D1确实属于这个M，就有效。
     */
    ReceiverDeliveryAckStatus
    Acknowledge(
        std::uint64_t message_id,
        std::uint64_t receiver_user_id,
        std::uint32_t delivery_seq
    );


    bool GetSnapshot(
        std::uint64_t message_id,
        ReceiverDeliverySnapshot* snapshot
    ) const;


    std::size_t Size() const;

    std::size_t ConfirmedCount() const;


private:
    enum class State {
        kWaitingAck = 0,
        kConfirmed
    };


    struct Entry {
        std::uint64_t receiver_user_id{0};

        std::uint32_t current_delivery_seq{0};

        std::uint32_t attempt_count{0};

        State state{
            State::kWaitingAck
        };


        /*
         * 所有真实发送过的Attempt。
         *
         * 后续Late ACK必须从这里验证。
         */
        std::unordered_set<
            std::uint32_t
        > attempt_seqs;
    };


    /*
     * mutex_已持有。
     */
    void TrimConfirmedLocked();


private:
    const std::size_t
        max_recent_confirmed_;


    /*
     * Gateway有多个Sub Reactor，
     * 后续M13还有多个Business Worker。
     *
     * 因此Tracker不能依赖单线程串行。
     */
    mutable std::mutex mutex_;


    std::unordered_map<
        std::uint64_t,
        Entry
    > entries_;


    /*
     * 只对Confirmed状态做有界缓存。
     *
     * WaitingAck后续由Timeout/Disconnect
     * 状态机负责收敛，不能在这里随意淘汰。
     */
    std::deque<std::uint64_t>
        confirmed_order_;
};


}  // namespace tinyimx