#pragma once

#include "common/concurrency/ThreadPool.h"
#include "gateway/business/BusinessRuntimeTypes.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace tinyimx {

struct BusinessExecutorOptions {
    std::size_t worker_threads{4};

    /*
     * 所有accepted但尚未完成的Task上限。
     *
     * 注意它不是单纯ThreadPool queue size。
     */
    std::size_t max_pending_tasks{128};

    /*
     * 固定stripe数量。
     *
     * M13冻结为64，运行过程中不动态扩缩。
     */
    std::size_t stripe_count{64};

    /*
     * 单个stripe最多积压多少个Task。
     *
     * 防止hot key拖垮整个Gateway。
     */
    std::size_t
        per_stripe_queue_capacity{32};

    std::chrono::milliseconds
        default_deadline{3000};

    std::chrono::milliseconds
        shutdown_timeout{30000};
};

struct BusinessExecutorStats {
    std::uint64_t submitted_total{0};
    std::uint64_t accepted_total{0};
    std::uint64_t completed_total{0};

    std::uint64_t
        rejected_overload_total{0};

    std::uint64_t
        rejected_hot_key_total{0};

    std::uint64_t
        rejected_deadline_total{0};

    std::uint64_t
        rejected_shutdown_total{0};

    std::uint64_t
        worker_exception_total{0};

    std::uint64_t
        deadline_expired_before_start_total{0};

    std::uint64_t
        cancelled_before_start_total{0};

    std::uint64_t
        completion_dropped_total{0};

    std::uint64_t
        rejected_invalid_total{0};

    std::uint64_t
        completion_exception_total{0};

    std::size_t current_pending_tasks{0};
    std::size_t peak_pending_tasks{0};

    std::size_t current_active_tasks{0};
    std::size_t peak_active_tasks{0};

    std::size_t pending_completions{0};

    double average_queue_wait_ms{0.0};
    double average_execution_ms{0.0};

    double max_queue_wait_ms{0.0};
    double max_execution_ms{0.0};

};

class BusinessExecutor final {
public:
    using CancellationProbe =
        std::function<bool()>;

    using Completion =
        std::function<void()>;

    using CompletionDispatcher =
        std::function<void(Completion)>;

    class ExecutionContext;

    using Work =
        std::function<
            Completion(
                const ExecutionContext&
            )
        >;

    struct TaskSpec {
        BusinessRequestContext request;

        BusinessCancellationPolicy
            cancellation_policy{
                BusinessCancellationPolicy::
                    kCancelable
            };

        /*
         * 返回true表示当前请求仍然具有继续执行资格。
         *
         * 例如History可以检查：
         * Session epoch是否仍然current。
         */
        CancellationProbe
            still_valid;

        /*
         * Completion投递资格与Work执行资格分离。
         *
         * kMustRun业务即使Session失效也必须完成Work，
         * 但旧Session的Completion仍然必须被fence。
         */
        CancellationProbe
            completion_still_valid;

        Work work;

        CompletionDispatcher
            dispatcher;
    };

public:
    explicit BusinessExecutor(
        BusinessExecutorOptions options
    );

    ~BusinessExecutor();

    BusinessExecutor(
        const BusinessExecutor&
    ) = delete;

    BusinessExecutor& operator=(
        const BusinessExecutor&
    ) = delete;

    bool Start();

    [[nodiscard]]
    BusinessSubmitStatus Submit(
        TaskSpec task
    );

    void BeginDrain();

    /*
     * 返回：
     * true  = 在shutdown budget内drain完成
     * false = 超过budget，但仍必须安全完成lifetime drain
     */
    bool ShutdownGraceful();

    bool IsAccepting() const noexcept;

    BusinessExecutorStats GetStats() const;

    class ExecutionContext {
    public:
        const BusinessRequestContext&
        Request() const noexcept {
            return request_;
        }

        bool DeadlineExpired() const noexcept;

        bool CancellationRequested()
            const;

    private:
        friend class BusinessExecutor;

        ExecutionContext(
            const BusinessRequestContext&
                request,
            BusinessCancellationPolicy
                policy,
            CancellationProbe probe
        );

        const BusinessRequestContext&
            request_;

        BusinessCancellationPolicy
            policy_;

        CancellationProbe probe_;
    };

private:
    struct TaskState;
    struct Stripe;

    BusinessSubmitStatus
    SubmitUnordered(
        std::shared_ptr<TaskState> task
    );

    BusinessSubmitStatus
    SubmitOrdered(
        std::shared_ptr<TaskState> task,
        BusinessOrderingKey key
    );

    void ExecuteTask(
        const std::shared_ptr<TaskState>&
            task
    );

    void DrainStripe(
        std::size_t stripe_index
    );

    bool ReservePending();
    void ReleasePending();

    bool CompletionStillValid(
        const std::shared_ptr<TaskState>& task
    ) noexcept;

    void DispatchCompletion(
        const std::shared_ptr<TaskState>& task,
        Completion completion
    );

    void UpdatePeak(
        std::atomic<std::size_t>& peak,
        std::size_t value
    );

    void UpdatePeakNs(
        std::atomic<std::uint64_t>& peak,
        std::uint64_t value
    );

    void RecordQueueWait(
        std::chrono::nanoseconds elapsed
    );

    void RecordExecution(
        std::chrono::nanoseconds elapsed
    );

    void NotifyDrainProgress();



private:
    BusinessExecutorOptions options_;

    std::unique_ptr<ThreadPool> pool_;


    /*
    * 固定数量的ordering stripes。
    *
    * 每个Stripe内部：
    * - 独立mutex
    * - FIFO queue
    * - running状态
    *
    * 使用unique_ptr的原因：
    * Stripe内部包含std::mutex，
    * 不适合要求元素可移动的vector<Stripe>。
    */
    std::vector<std::unique_ptr<Stripe>>
        stripes_;


    std::atomic<bool>
        accepting_{false};


        /*
    * Submit与BeginDrain之间的Admission Fence。
    *
    * TrySubmit本身non-blocking，
    * 所以持锁区非常短。
    *
    * 目标：
    * BeginDrain()一旦返回，
    * 后续Submit绝不能再成功进入Runtime。
    */
    mutable std::mutex admission_mutex_;

    mutable std::mutex drain_mutex_;

    std::condition_variable drain_cv_;

    std::atomic<std::uint64_t>
        submitted_total_{0};

    std::atomic<std::uint64_t>
        accepted_total_{0};

    std::atomic<std::uint64_t>
        completed_total_{0};

    std::atomic<std::uint64_t>
        rejected_overload_total_{0};

    std::atomic<std::uint64_t>
        rejected_hot_key_total_{0};

    std::atomic<std::uint64_t>
        rejected_deadline_total_{0};

    std::atomic<std::uint64_t>
        rejected_shutdown_total_{0};

    std::atomic<std::uint64_t>
        rejected_invalid_total_{0};

    std::atomic<std::uint64_t>
        deadline_expired_before_start_total_{0};

    std::atomic<std::uint64_t>
        cancelled_before_start_total_{0};

    std::atomic<std::uint64_t>
        worker_exception_total_{0};

    std::atomic<std::uint64_t>
        completion_exception_total_{0};

    std::atomic<std::uint64_t>
        completion_dropped_total_{0};


    std::atomic<std::size_t>
        current_pending_tasks_{0};

    std::atomic<std::size_t>
        peak_pending_tasks_{0};

    std::atomic<std::size_t>
        current_active_tasks_{0};

    std::atomic<std::size_t>
        peak_active_tasks_{0};

    std::atomic<std::size_t>
        pending_completions_{0};

    std::atomic<std::uint64_t>
        total_queue_wait_ns_{0};

    std::atomic<std::uint64_t>
        queue_wait_sample_count_{0};

    std::atomic<std::uint64_t>
        max_queue_wait_ns_{0};

    std::atomic<std::uint64_t>
        total_execution_ns_{0};

    std::atomic<std::uint64_t>
        execution_sample_count_{0};

    std::atomic<std::uint64_t>
        max_execution_ns_{0};
};

}  // namespace tinyimx