#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace tinyimx {


enum class GatewayPeerDedupBeginStatus {
    /*
     * 当前调用者成功获得
     * message_id 的唯一执行权。
     */
    kAcquired = 0,

    /*
     * 同一个 message_id
     * 已经有别的线程正在处理。
     */
    kAlreadyProcessing,

    /*
     * 同一个 message_id
     * 以前已经成功投递。
     */
    kAlreadyDelivered,

    /*
     * message_id == 0 等
     * 非法参数。
     */
    kInvalidArgument
};


class GatewayPeerDeliveryDeduplicator {
public:
    explicit GatewayPeerDeliveryDeduplicator(
        std::size_t max_recent_delivered =
            10000
    );


    GatewayPeerDeliveryDeduplicator(
        const GatewayPeerDeliveryDeduplicator&
    ) = delete;

    GatewayPeerDeliveryDeduplicator&
    operator=(
        const GatewayPeerDeliveryDeduplicator&
    ) = delete;


    /*
     * 尝试获得一条业务消息的执行权。
     *
     * 不存在：
     *   插入 Processing
     *   返回 kAcquired
     *
     * Processing：
     *   返回 kAlreadyProcessing
     *
     * Delivered：
     *   返回 kAlreadyDelivered
     */
    GatewayPeerDedupBeginStatus Begin(
        std::uint64_t message_id
    );


    /*
     * 真正产生消息投递副作用后调用。
     *
     * Processing
     *      ↓
     * Delivered
     */
    bool MarkDelivered(
        std::uint64_t message_id
    );


    /*
     * 当前执行失败，而且确定
     * 没有产生消息投递副作用时调用。
     *
     * Processing
     *      ↓
     * 删除
     *
     * 从而允许未来Retry重新执行。
     */
    bool Abort(
        std::uint64_t message_id
    );


    /*
     * 当前维护的全部状态数量：
     *
     * Processing + Delivered
     */
    std::size_t Size() const;


    std::size_t DeliveredCount()
        const;


private:
    enum class State {
        kProcessing = 0,
        kDelivered
    };


    /*
     * mutex_已经被持有时调用。
     */
    void TrimDeliveredLocked();


private:
    /*
     * 防止Delivered记录永久增长。
     */
    const std::size_t
        max_recent_delivered_;


    /*
     * 为什么这里需要mutex？
     *
     * Gateway内部连接也运行在
     * 多个Sub-Reactor，
     * Begin可能被多个线程同时调用。
     */
    mutable std::mutex mutex_;


    std::unordered_map<
        std::uint64_t,
        State
    > states_;


    /*
     * 记录Delivered消息的完成顺序。
     *
     * 超过容量以后淘汰最老记录。
     */
    std::deque<std::uint64_t>
        delivered_order_;
};

}  // namespace tinyimx