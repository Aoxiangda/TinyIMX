#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>


namespace tinyimx {


/*
 * 一条server message在当前Gateway进程内
 * 尝试获取“投递副作用执行权”时的结果。
 *
 * 注意：
 *
 * 这个状态不是数据库DeliveryStatus。
 *
 * DeliveryStatus描述持久化业务状态；
 * MessageDeliveryDedupBeginStatus描述
 * 当前进程内谁有资格执行SendPacket副作用。
 */
enum class MessageDeliveryDedupBeginStatus {
    /*
     * 当前调用者成功获得
     * message_id 的唯一执行权。
     */
    kAcquired = 0,


    /*
     * 同一个message_id
     * 已经由其他线程执行投递。
     */
    kAlreadyProcessing,


    /*
     * 同一个message_id
     * 已经在当前进程内完成过投递。
     */
    kAlreadyDelivered,


    /*
     * message_id == 0等非法输入。
     */
    kInvalidArgument
};


/*
 * Gateway进程内的消息投递执行权控制器。
 *
 * 核心职责：
 *
 * 1. 同一个message_id同一时刻只能有一个Owner；
 * 2. 已完成投递的message_id禁止重复执行副作用；
 * 3. 明确未产生副作用的失败允许Abort后Retry；
 * 4. Delivered热状态有界保存，避免无限增长。
 *
 * 它不负责：
 *
 * - MySQL持久化幂等；
 * - Client client_message_id唯一性；
 * - 跨进程/跨重启的最终幂等需要依赖
 *   MySQL中的稳定message_id与持久状态，
 *   本类自身不提供Exactly-Once语义；
 * - Receiver ACK。
 *
 * 它解决的是：
 *
 * “当前Gateway进程中，同一条server message
 *  谁有资格执行一次SendPacket？”
 */
class MessageDeliveryDeduplicator {
public:
    explicit MessageDeliveryDeduplicator(
        std::size_t max_recent_delivered =
            10000
    );


    MessageDeliveryDeduplicator(
        const MessageDeliveryDeduplicator&
    ) = delete;


    MessageDeliveryDeduplicator&
    operator=(
        const MessageDeliveryDeduplicator&
    ) = delete;


    /*
     * 尝试获得message_id的投递执行权。
     *
     * NotSeen
     *   ↓ Begin
     * Processing
     *   → kAcquired
     *
     * Processing
     *   → kAlreadyProcessing
     *
     * Delivered
     *   → kAlreadyDelivered
     */
    MessageDeliveryDedupBeginStatus Begin(
        std::uint64_t message_id
    );


    /*
     * 确认已经产生真实投递副作用。
     *
     * Processing
     *      ↓
     * Delivered
     */
    bool MarkDelivered(
        std::uint64_t message_id
    );


    /*
     * 只有确定没有产生投递副作用时
     * 才允许Abort。
     *
     * Processing
     *      ↓
     * NotSeen
     *
     * 未来Retry因此可以重新获得Owner。
     */
    bool Abort(
        std::uint64_t message_id
    );


    /*
     * Processing + Delivered总数量。
     */
    std::size_t Size() const;


    /*
     * 当前热缓存中的Delivered数量。
     */
    std::size_t DeliveredCount() const;


private:
    enum class State {
        kProcessing = 0,
        kDelivered
    };


    /*
     * 调用时要求mutex_已经持有。
     */
    void TrimDeliveredLocked();


private:
    /*
     * Delivered状态只保留最近固定数量，
     * 防止长时间运行后内存无限增长。
     */
    const std::size_t
        max_recent_delivered_;


    /*
     * Begin的find + insert必须原子。
     *
     * 后续M13把业务逻辑下沉到ThreadPool后，
     * 多Worker也可能同时竞争同一个message_id，
     * 因此这里不能依赖Reactor串行性。
     */
    mutable std::mutex mutex_;


    std::unordered_map<
        std::uint64_t,
        State
    > states_;


    /*
     * Delivered消息完成顺序。
     *
     * 用于有界淘汰最老记录。
     */
    std::deque<std::uint64_t>
        delivered_order_;
};


}  // namespace tinyimx