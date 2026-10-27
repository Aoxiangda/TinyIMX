#include "gateway/business/BusinessExecutor.h"

#include "common/logging/LogMacros.h"

#include <chrono>
#include <exception>
#include <limits>
#include <unordered_map>
#include <utility>

namespace tinyimx {

struct BusinessExecutor::TaskState {
    TaskSpec spec;

    BusinessTimePoint enqueued_at{
        BusinessClock::now()
    };
    std::optional<std::size_t> async_stripe;
    std::optional<BusinessOrderingKey> async_key;
    std::atomic<bool> async_finished{false};
};

struct BusinessExecutor::KeyLane {
    // Accessed only with the owning Stripe::mutex held. Presence in lanes
    // includes a queued/running/suspended drainer; it is the ordering fence.
    std::deque<std::shared_ptr<TaskState>> queue;
};

struct BusinessExecutor::Stripe {
    std::mutex mutex;

    std::deque<
        std::shared_ptr<TaskState>
    > queue;

    /*
     * true：
     * 当前已经有一个Worker负责drain这个Stripe。
     *
     * 所以绝对不能再启动第二个Worker并发处理同Stripe。
     */
    bool running{false};
    std::unordered_map<BusinessOrderingKey,KeyLane> lanes;
    std::size_t isolated_pending{0};
};


BusinessExecutor::ExecutionContext::
ExecutionContext(
    const BusinessRequestContext& request,
    BusinessCancellationPolicy policy,
    CancellationProbe probe
)
    : request_(request),
      policy_(policy),
      probe_(std::move(probe)) {
}

bool BusinessExecutor::
ExecutionContext::DeadlineExpired()
    const noexcept {
    return request_.DeadlineExpired();
}

bool BusinessExecutor::
ExecutionContext::CancellationRequested()
    const {
    if (
        policy_ ==
        BusinessCancellationPolicy::
            kMustRun
    ) {
        return false;
    }

    if (DeadlineExpired()) {
        return true;
    }

    if (probe_ && !probe_()) {
        return true;
    }

    return false;
}

BusinessExecutor::BusinessExecutor(
    BusinessExecutorOptions options
)
    : options_(std::move(options)) {
}

BusinessExecutor::~BusinessExecutor() {
    if (pool_) {
        ShutdownGraceful();
    }
}

bool BusinessExecutor::Start() {
    if (pool_) {
        return false;
    }

    if (
        options_.worker_threads == 0 ||
        options_.max_pending_tasks > std::numeric_limits<std::size_t>::max()/4 ||
        options_.stripe_count > std::numeric_limits<std::size_t>::max()/4 ||
        options_.worker_threads > std::numeric_limits<std::size_t>::max()/4 ||
        options_.max_pending_tasks == 0 ||
        options_.stripe_count == 0 ||
        options_.per_stripe_queue_capacity == 0 ||
        options_.per_stripe_queue_capacity >
            options_.max_pending_tasks ||
        options_.default_deadline.count() <= 0 ||
        options_.shutdown_timeout.count() <= 0
    ) {
        return false;
    }

    /*
    * Start阶段一次性创建固定数量Stripe。
    *
    * Runtime运行过程中不动态增加/删除Stripe，
    * 避免ordering key在运行期间重新映射。
    */
    stripes_.clear();

    stripes_.reserve(
        options_.stripe_count
    );

    for (
        std::size_t i = 0;
        i < options_.stripe_count;
        ++i
    ) {
        stripes_.push_back(
            std::make_unique<Stripe>()
        );
    }


    ThreadPoolOptions pool_options;

    pool_options.name = options_.name;

    pool_options.worker_threads =
        options_.worker_threads;

    pool_options.queue_capacity =
        options_.max_pending_tasks * 2 + options_.stripe_count + options_.worker_threads;

    /*
     * BusinessExecutor只通过TrySubmit提交。
     *
     * 这里仍设置Discard，
     * 是为了让底层配置语义与运行语义一致。
     */
    pool_options.queue_full_policy =
        QueueFullPolicy::kDiscard;

    pool_options.enable_dynamic_resize =
        false;

    pool_ =
        std::make_unique<ThreadPool>(
            std::move(pool_options)
        );

    if (!pool_->Start()) {
        pool_.reset();
        stripes_.clear();

        return false;
    }

    accepting_.store(
        true,
        std::memory_order_release
    );

    LOG_INFO(
        "business executor started"
        << ", name=" << options_.name
        << ", workers="
        << options_.worker_threads
        << ", max_pending="
        << options_.max_pending_tasks
        << ", stripes="
        << options_.stripe_count
        << ", per_stripe_capacity="
        << options_.per_stripe_queue_capacity
        << ", isolate_ordering_keys=" << options_.isolate_ordering_keys
        << ", default_deadline_ms="
        << options_.default_deadline.count()
        << ", shutdown_timeout_ms="
        << options_.shutdown_timeout.count()
    );

    return true;
}

void BusinessExecutor::BeginDrain() {
    std::lock_guard<std::mutex>
        lock(admission_mutex_);

    accepting_.store(
        false,
        std::memory_order_release
    );
}

bool BusinessExecutor::ShutdownGraceful() {
    /*
     * Budget覆盖从ShutdownGraceful进入开始的完整生命周期，
     * 包括Admission Fence、Worker drain与Completion drain。
     */
    const auto begin =
        BusinessClock::now();

    const auto deadline =
        begin + options_.shutdown_timeout;

    /*
     * BeginDrain()返回以后，新的Submit不能再进入Runtime。
     */
    BeginDrain();

    /*
     * kGraceful不会为了满足SLA强制丢弃已经accepted的任务。
     *
     * Business Runtime的kMustRun语义要求：
     * accepted durable responsibility必须安全完成。
     */
    // Keep workers available for accepted RPC continuations until logical drain.

    bool within_budget =
        BusinessClock::now() <= deadline;

    const auto drained =
        [this]() {
            return
                current_pending_tasks_.load(
                    std::memory_order_acquire
                ) == 0 &&
                pending_completions_.load(
                    std::memory_order_acquire
                ) == 0 && pending_async_tasks_.load(std::memory_order_acquire)==0 &&
                pending_internal_jobs_.load(std::memory_order_acquire)==0;
        };

    {
        std::unique_lock<std::mutex>
            lock(drain_mutex_);

        if (!drained()) {
            bool drained_in_time = false;

            if (BusinessClock::now() < deadline) {
                drained_in_time =
                    drain_cv_.wait_until(
                        lock,
                        deadline,
                        drained
                    );
            }

            if (!drained_in_time) {
                within_budget = false;

                LOG_WARN(
                    "business executor drain budget exceeded"
                    << ", pending_tasks="
                    << current_pending_tasks_.load(
                           std::memory_order_relaxed
                       )
                    << ", pending_completions="
                    << pending_completions_.load(
                           std::memory_order_relaxed
                       )
                );

                /*
                 * Safety > shutdown SLA。
                 *
                 * budget是运行SLA，不是强制取消边界。
                 * 即使超时也要等accepted Work和已dispatch Completion
                 * 全部退出生命周期，避免Repository/Gateway被提前析构。
                 */
                drain_cv_.wait(
                    lock,
                    drained
                );
            }
        }
    }

    if (BusinessClock::now() > deadline) {
        within_budget = false;
    }

    if(pool_)pool_->Shutdown(ShutdownMode::kGraceful,options_.shutdown_timeout);
    pool_.reset();

    LOG_INFO(
        "business executor stopped"
        << ", within_budget="
        << within_budget
        << ", shutdown_elapsed_ms="
        << std::chrono::duration_cast<
               std::chrono::milliseconds
           >(
               BusinessClock::now() - begin
           ).count()
    );

    const auto final_stats=GetStats();
    if(final_stats.rejected_overload_total) {
        LOG_WARN("business executor overload summary, name="<<options_.name
            <<", rejected_overload="<<final_stats.rejected_overload_total
            <<", pending_budget="<<final_stats.rejected_pending_budget_total
            <<", pool_submission="<<final_stats.rejected_pool_submission_total
            <<", key_allocation="<<final_stats.rejected_key_allocation_total
            <<", peak_pending="<<final_stats.peak_pending_tasks
            <<", peak_active="<<final_stats.peak_active_tasks);
    }
    return within_budget;
}

bool BusinessExecutor::IsAccepting()
    const noexcept {
    return
        accepting_.load(
            std::memory_order_acquire
        );
}

BusinessExecutorStats
BusinessExecutor::GetStats() const {
    BusinessExecutorStats stats;
    stats.rejected_pending_budget_total=rejected_pending_budget_total_.load(std::memory_order_relaxed);
    stats.rejected_pool_submission_total=rejected_pool_submission_total_.load(std::memory_order_relaxed);
    stats.rejected_key_allocation_total=rejected_key_allocation_total_.load(std::memory_order_relaxed);
    stats.async_started_total=async_started_total_.load(std::memory_order_relaxed);
    stats.async_finished_total=async_finished_total_.load(std::memory_order_relaxed);
    stats.continuation_run_total=continuation_run_total_.load(std::memory_order_relaxed);
    stats.current_async_tasks=pending_async_tasks_.load(std::memory_order_relaxed);

    stats.submitted_total =
        submitted_total_.load(
            std::memory_order_relaxed
        );

    stats.accepted_total =
        accepted_total_.load(
            std::memory_order_relaxed
        );

    stats.completed_total =
        completed_total_.load(
            std::memory_order_relaxed
        );

    stats.rejected_overload_total =
        rejected_overload_total_.load(
            std::memory_order_relaxed
        );

    stats.rejected_hot_key_total =
        rejected_hot_key_total_.load(
            std::memory_order_relaxed
        );

    stats.rejected_deadline_total =
        rejected_deadline_total_.load(
            std::memory_order_relaxed
        );

    stats.rejected_shutdown_total =
        rejected_shutdown_total_.load(
            std::memory_order_relaxed
        );

    stats.rejected_invalid_total =
        rejected_invalid_total_.load(
            std::memory_order_relaxed
        );

    stats.deadline_expired_before_start_total =
        deadline_expired_before_start_total_.
            load(
                std::memory_order_relaxed
            );

    stats.cancelled_before_start_total =
        cancelled_before_start_total_.load(
            std::memory_order_relaxed
        );

    stats.deadline_terminal_callback_total =
        deadline_terminal_callback_total_.load(
            std::memory_order_relaxed
        );

    stats.deadline_terminal_dropped_total =
        deadline_terminal_dropped_total_.load(
            std::memory_order_relaxed
        );

    stats.deadline_terminal_exception_total =
        deadline_terminal_exception_total_.load(
            std::memory_order_relaxed
        );

    stats.worker_exception_total =
        worker_exception_total_.load(
            std::memory_order_relaxed
        );

    stats.completion_exception_total =
        completion_exception_total_.load(
            std::memory_order_relaxed
        );

    stats.completion_dropped_total =
        completion_dropped_total_.load(
            std::memory_order_relaxed
        );

    stats.current_pending_tasks =
        current_pending_tasks_.load(
            std::memory_order_relaxed
        );

    stats.peak_pending_tasks =
        peak_pending_tasks_.load(
            std::memory_order_relaxed
        );

    stats.current_active_tasks =
        current_active_tasks_.load(
            std::memory_order_relaxed
        );

    stats.peak_active_tasks =
        peak_active_tasks_.load(
            std::memory_order_relaxed
        );

    stats.pending_completions =
        pending_completions_.load(
            std::memory_order_relaxed
        );


    const auto queue_samples =
        queue_wait_sample_count_.load(
            std::memory_order_relaxed
        );

    if (queue_samples > 0) {
        stats.average_queue_wait_ms =
            static_cast<double>(
                total_queue_wait_ns_.load(
                    std::memory_order_relaxed
                )
            ) /
            static_cast<double>(
                queue_samples
            ) /
            1000000.0;
    }


    stats.max_queue_wait_ms =
        static_cast<double>(
            max_queue_wait_ns_.load(
                std::memory_order_relaxed
            )
        ) /
        1000000.0;


    const auto execution_samples =
        execution_sample_count_.load(
            std::memory_order_relaxed
        );

    if (execution_samples > 0) {
        stats.average_execution_ms =
            static_cast<double>(
                total_execution_ns_.load(
                    std::memory_order_relaxed
                )
            ) /
            static_cast<double>(
                execution_samples
            ) /
            1000000.0;
    }


    stats.max_execution_ms =
        static_cast<double>(
            max_execution_ns_.load(
                std::memory_order_relaxed
            )
        ) /
        1000000.0;


    return stats;
}


BusinessSubmitStatus
BusinessExecutor::Submit(
    TaskSpec task
) {
    submitted_total_.fetch_add(
        1,
        std::memory_order_relaxed
    );

    /*
     * 没有Work的Task没有任何业务意义。
     */
    if (static_cast<bool>(task.work)==static_cast<bool>(task.async_work)) {
        rejected_invalid_total_.fetch_add(
            1,
            std::memory_order_relaxed
        );

        return
            BusinessSubmitStatus::
                kInvalidArgument;
    }


    const auto now =
        BusinessClock::now();


    /*
     * 调用方没有给显式deadline时，
     * Runtime施加统一默认deadline。
     */
    if (!task.request.HasDeadline()) {
        task.request.deadline =
            task.request.received_at +
            options_.default_deadline;
    }


    /*
     * Submit发生时就已经超时：
     *
     * 根本不要占Runtime容量。
     */
    if (task.request.DeadlineExpired()) {
        rejected_deadline_total_.fetch_add(
            1,
            std::memory_order_relaxed
        );

        return
            BusinessSubmitStatus::
                kDeadlineExpired;
    }


    auto state =
        std::make_shared<TaskState>();

    state->spec =
        std::move(task);

    state->enqueued_at = now;
    state->spec.request.executor_submitted_at = now;
    state->spec.request.executor_enqueued_at = {};
    state->spec.request.executor_started_at = {};


    /*
     * 非常短的Admission临界区。
     *
     * 里面只有：
     * accepting check
     * capacity reserve
     * non-blocking TrySubmit
     *
     * 不允许任何业务IO进入这里。
     */
    std::lock_guard<std::mutex>
        lock(admission_mutex_);


    if (
        !accepting_.load(
            std::memory_order_acquire
        ) ||
        !pool_
    ) {
        rejected_shutdown_total_.fetch_add(
            1,
            std::memory_order_relaxed
        );

        return
            BusinessSubmitStatus::
                kShuttingDown;
    }


   if (!ReservePending()) {
        rejected_pending_budget_total_.fetch_add(1,std::memory_order_relaxed);
        rejected_overload_total_.fetch_add(
            1,
            std::memory_order_relaxed
        );

        return
            BusinessSubmitStatus::
                kOverloaded;
    }


    /*
    * ordered / unordered统一从这里分流。
    */
    BusinessSubmitStatus status;

    if (
        state->spec.request.
            ordering_key.has_value()
    ) {
        status =
            SubmitOrdered(
                state,
                *state->spec.request.
                    ordering_key
            );
    } else {
        status =
            SubmitUnordered(
                state
            );
    }


    /*
    * 投递没有被Runtime真正接受：
    * 撤销global pending reservation。
    */
    if (
        status !=
        BusinessSubmitStatus::kAccepted
    ) {
        ReleasePending();


        switch (status) {
            case BusinessSubmitStatus::
                kHotKeyOverloaded:
                rejected_hot_key_total_.
                    fetch_add(
                        1,
                        std::memory_order_relaxed
                    );
                break;


            case BusinessSubmitStatus::
                kOverloaded:
                rejected_overload_total_.
                    fetch_add(
                        1,
                        std::memory_order_relaxed
                    );
                break;


            case BusinessSubmitStatus::
                kShuttingDown:
                rejected_shutdown_total_.
                    fetch_add(
                        1,
                        std::memory_order_relaxed
                    );
                break;


            default:
                rejected_invalid_total_.
                    fetch_add(
                        1,
                        std::memory_order_relaxed
                    );
                break;
        }


        return status;
    }


    accepted_total_.fetch_add(
        1,
        std::memory_order_relaxed
    );

    return
        BusinessSubmitStatus::
            kAccepted;
}


BusinessSubmitStatus
BusinessExecutor::SubmitUnordered(
    std::shared_ptr<TaskState> task
) {
    if (!task || !pool_) {
        return
            BusinessSubmitStatus::
                kInvalidArgument;
    }

    task->spec.request.executor_enqueued_at = BusinessClock::now();
    const auto push_result =
        pool_->TrySubmit(
            [
                this,
                task = std::move(task)
            ]() {
                ExecuteTask(task);
            }
        );

    if (
        push_result ==
        TaskPushResult::kOk
    ) {
        return
            BusinessSubmitStatus::
                kAccepted;
    }

    if (
        push_result ==
        TaskPushResult::kStopped
    ) {
        return
            BusinessSubmitStatus::
                kShuttingDown;
    }

    rejected_pool_submission_total_.fetch_add(1,std::memory_order_relaxed);
    return
        BusinessSubmitStatus::
            kOverloaded;
}


BusinessSubmitStatus
BusinessExecutor::SubmitOrdered(
    std::shared_ptr<TaskState> task,
    BusinessOrderingKey key
) {
    if (options_.isolate_ordering_keys) {
        return SubmitIsolatedKey(std::move(task),key);
    }
    if (
        !task ||
        stripes_.empty() ||
        !pool_
    ) {
        return
            BusinessSubmitStatus::
                kInvalidArgument;
    }


    const std::size_t stripe_index =
        static_cast<std::size_t>(
            key % stripes_.size()
        );


    Stripe& stripe =
        *stripes_[stripe_index];


    bool need_schedule = false;


    {
        std::lock_guard<std::mutex>
            lock(stripe.mutex);


        /*
         * running中的Task也占一个hot-key slot。
         *
         * 例如capacity=2：
         *
         * 1 running
         * 1 queued
         *
         * 第3个就应该拒绝。
         */
        const std::size_t stripe_pending =
            stripe.queue.size() +
            (stripe.running ? 1U : 0U);


        if (
            stripe_pending >=
            options_.
                per_stripe_queue_capacity
        ) {
            return
                BusinessSubmitStatus::
                    kHotKeyOverloaded;
        }


        task->spec.request.executor_enqueued_at = BusinessClock::now();
        stripe.queue.push_back(
            std::move(task)
        );


        /*
         * 如果已经有人负责drain，
         * 这里只需要入队。
         *
         * 绝不能再起第二个drainer。
         */
        if (!stripe.running) {
            stripe.running = true;
            need_schedule = true;
        }
    }


    if (!need_schedule) {
        return
            BusinessSubmitStatus::
                kAccepted;
    }


    const auto push_result =
        pool_->TrySubmit(
            [
                this,
                stripe_index
            ]() {
                DrainStripe(
                    stripe_index
                );
            }
        );


    if (
        push_result ==
        TaskPushResult::kOk
    ) {
        return
            BusinessSubmitStatus::
                kAccepted;
    }


    /*
     * 第一个stripe drainer都没有成功进入ThreadPool。
     *
     * 当前Submit仍然处于admission_mutex保护下，
     * 所以不会同时有新的Submit修改这个Stripe。
     */
    {
        std::lock_guard<std::mutex>
            lock(stripe.mutex);

        stripe.queue.clear();
        stripe.running = false;
    }


    if (
        push_result ==
        TaskPushResult::kStopped
    ) {
        return
            BusinessSubmitStatus::
                kShuttingDown;
    }


    rejected_pool_submission_total_.fetch_add(1,std::memory_order_relaxed);
    return
        BusinessSubmitStatus::
            kOverloaded;
}


BusinessSubmitStatus BusinessExecutor::SubmitIsolatedKey(
    std::shared_ptr<TaskState> task, BusinessOrderingKey key) {
    if (!task || stripes_.empty() || !pool_) {
        return BusinessSubmitStatus::kInvalidArgument;
    }
    const auto index=static_cast<std::size_t>(key%stripes_.size());
    auto& stripe=*stripes_[index];
    bool inserted=false;
    {
        std::lock_guard lock(stripe.mutex);
        // The old aggregate stripe bound is retained, not multiplied per key.
        if (stripe.isolated_pending>=options_.per_stripe_queue_capacity) {
            return BusinessSubmitStatus::kHotKeyOverloaded;
        }
        try {
            auto [it,created]=stripe.lanes.try_emplace(key);
            inserted=created;
            task->spec.request.executor_enqueued_at = BusinessClock::now();
            try { it->second.queue.push_back(std::move(task)); }
            catch (...) {
                if(created)stripe.lanes.erase(it);
                throw;
            }
        }catch(const std::bad_alloc&) {
            rejected_key_allocation_total_.fetch_add(1,std::memory_order_relaxed);
            return BusinessSubmitStatus::kOverloaded;
        }
        ++stripe.isolated_pending;
    }
    if (!inserted) return BusinessSubmitStatus::kAccepted;
    const auto pushed=pool_->TrySubmit([this,index,key]{DrainKey(index,key);});
    if (pushed==TaskPushResult::kOk) return BusinessSubmitStatus::kAccepted;
    {
        // Submit still holds admission_mutex_: no later submit can have joined
        // this new lane. Other keys in this stripe are left completely intact.
        std::lock_guard lock(stripe.mutex);
        stripe.lanes.erase(key);
        --stripe.isolated_pending;
    }
    if(pushed!=TaskPushResult::kStopped)rejected_pool_submission_total_.fetch_add(1,std::memory_order_relaxed);
    return pushed==TaskPushResult::kStopped
        ? BusinessSubmitStatus::kShuttingDown : BusinessSubmitStatus::kOverloaded;
}

bool BusinessExecutor::CompleteKeyTask(std::size_t index,BusinessOrderingKey key) {
    auto& stripe=*stripes_[index];
    std::lock_guard lock(stripe.mutex);
    const auto it=stripe.lanes.find(key);
    if(it==stripe.lanes.end() || stripe.isolated_pending==0)
        throw std::logic_error("ordered key completion without a reserved lane");
    --stripe.isolated_pending;
    if(it->second.queue.empty()) {
        // Retire an empty lane in the same critical section that releases its
        // last slot. Do not accumulate empty lanes or queued no-op drainers as
        // completed async tasks make room for newly admitted keys.
        stripe.lanes.erase(it);
        return false;
    }
    return true;
}

void BusinessExecutor::DrainKey(std::size_t index,BusinessOrderingKey key) {
    auto& stripe=*stripes_[index];
    while (true) {
        std::shared_ptr<TaskState> task;
        {
            std::lock_guard lock(stripe.mutex);
            const auto it=stripe.lanes.find(key);
            if (it==stripe.lanes.end()) return;
            if (it->second.queue.empty()) {
                // Empty+erase under the same lock prevents lost wakeups. The
                // lane map cannot grow beyond the existing pending budgets.
                stripe.lanes.erase(it);
                return;
            }
            task=std::move(it->second.queue.front());
            it->second.queue.pop_front();
        }
        if (task->spec.async_work) {
            task->async_stripe=index;
            task->async_key=key;
        }
        if (!ExecuteTask(task)) return; // Async completion retains this lane.
        // Includes cancelled/expired-before-start tasks. A newly submitted key
        // can only create a replacement lane after this work has completed.
        if(!CompleteKeyTask(index,key))return;
    }
}

void BusinessExecutor::DrainStripe(
    std::size_t stripe_index
) {
    if (
        stripe_index >=
        stripes_.size()
    ) {
        return;
    }


    Stripe& stripe =
        *stripes_[stripe_index];


    while (true) {
        std::shared_ptr<TaskState> task;


        {
            std::lock_guard<std::mutex>
                lock(stripe.mutex);


            if (stripe.queue.empty()) {
                /*
                 * 所有这个key/stripe上的Task已经处理完。
                 *
                 * 必须在同一个mutex临界区里：
                 *
                 * queue empty
                 * +
                 * running=false
                 *
                 * 避免Submit与drainer退出产生lost wakeup。
                 */
                stripe.running = false;

                return;
            }


            task =
                std::move(
                    stripe.queue.front()
                );

            stripe.queue.pop_front();
        }


        /*
         * Stripe mutex绝对不能带进业务代码。
         *
         * 因为ExecuteTask里面可能：
         *
         * MySQL
         * Redis
         * future gRPC
         *
         * 如果持mutex执行，
         * Submit同Stripe也会被慢SQL阻塞。
         */
        if(task->spec.async_work)task->async_stripe=stripe_index;
        if(!ExecuteTask(task))return; // logical completion schedules the next drainer
    }
}


bool BusinessExecutor::ExecuteTask(
    const std::shared_ptr<TaskState>& task
) {
    if (!task) {
        return true;
    }


    const auto started_at =
        BusinessClock::now();

    task->spec.request.executor_started_at = started_at;

    RecordQueueWait(
        std::chrono::duration_cast<
            std::chrono::nanoseconds
        >(
            started_at -
            task->enqueued_at
        )
    );


    ExecutionContext context(
        task->spec.request,
        task->spec.cancellation_policy,
        task->spec.still_valid
    );


    /*
     * kCancelable：
     *
     * Task排队期间可能已经：
     * - deadline expired
     * - session replaced
     * - client disconnected
     *
     * 此时不要占DB连接。
     */
    if (
        task->spec.cancellation_policy ==
            BusinessCancellationPolicy::
                kCancelable
    ) {
        if (
            task->spec.request.
                DeadlineExpired()
        ) {
            deadline_expired_before_start_total_.
                fetch_add(
                    1,
                    std::memory_order_relaxed
                );

            DispatchDeadlineTerminal(task);

            return true;
        }


        if (
            task->spec.still_valid &&
            !task->spec.still_valid()
        ) {
            cancelled_before_start_total_.
                fetch_add(
                    1,
                    std::memory_order_relaxed
                );

            ReleasePending();

            return true;
        }
    }


    const std::size_t active =
        current_active_tasks_.fetch_add(
            1,
            std::memory_order_acq_rel
        ) + 1;

    UpdatePeak(
        peak_active_tasks_,
        active
    );


    const auto execution_begin =
        BusinessClock::now();

    if(task->spec.async_work) {
        pending_async_tasks_.fetch_add(1,std::memory_order_acq_rel);
        async_started_total_.fetch_add(1,std::memory_order_relaxed);
        try {
            task->spec.async_work(context,
                [this,task](Completion fn) {
                    if(task->async_finished.load(std::memory_order_acquire))return;
                    PostInternal([this,task,fn=std::move(fn)]()mutable {
                        if(task->async_finished.load(std::memory_order_acquire))return;
                        continuation_run_total_.fetch_add(1,std::memory_order_relaxed);
                        const auto active=current_active_tasks_.fetch_add(1,std::memory_order_acq_rel)+1;
                        UpdatePeak(peak_active_tasks_,active);
                        const auto begin=BusinessClock::now();
                        try{fn();}catch(...){
                            current_active_tasks_.fetch_sub(1,std::memory_order_acq_rel);throw;
                        }
                        current_active_tasks_.fetch_sub(1,std::memory_order_acq_rel);
                        RecordExecution(std::chrono::duration_cast<std::chrono::nanoseconds>(BusinessClock::now()-begin));
                    });
                },
                [this,task](Completion completed,std::exception_ptr error) {
                    FinishAsync(task,std::move(completed),error);
                });
        }catch(...){FinishAsync(task,{},std::current_exception());}
        current_active_tasks_.fetch_sub(1,std::memory_order_acq_rel);
        RecordExecution(std::chrono::duration_cast<std::chrono::nanoseconds>(BusinessClock::now()-execution_begin));
        return false; // caller releases its OS thread; task still owns the stripe
    }

    Completion completion;

    bool work_succeeded = false;


    try {
        completion =
            task->spec.work(
                context
            );

        work_succeeded = true;
    } catch (const std::exception& e) {
        worker_exception_total_.fetch_add(
            1,
            std::memory_order_relaxed
        );

        LOG_ERROR(
            "business task failed"
            << ", operation="
            << task->spec.request.operation
            << ", user_id="
            << task->spec.request.user_id
            << ", request_seq="
            << task->spec.request.request_seq
            << ", error="
            << e.what()
        );
    } catch (...) {
        worker_exception_total_.fetch_add(
            1,
            std::memory_order_relaxed
        );

        LOG_ERROR(
            "business task failed"
            << ", operation="
            << task->spec.request.operation
            << ", user_id="
            << task->spec.request.user_id
            << ", request_seq="
            << task->spec.request.request_seq
            << ", error=unknown_exception"
        );
    }


    const auto execution_end =
        BusinessClock::now();

    RecordExecution(
        std::chrono::duration_cast<
            std::chrono::nanoseconds
        >(
            execution_end -
            execution_begin
        )
    );


    current_active_tasks_.fetch_sub(
        1,
        std::memory_order_acq_rel
    );


    if (!work_succeeded) {
        ReleasePending();
        return true;
    }


    /*
     * 有些内部Task可能没有网络Completion。
     */
    if (!completion) {
        completed_total_.fetch_add(
            1,
            std::memory_order_relaxed
        );

        ReleasePending();

        return true;
    }


    DispatchCompletion(
        task,
        std::move(completion)
    );
    return true;
}



void BusinessExecutor::PostInternal(Completion fn) {
    // External pending remains bounded. Each accepted async task has at most
    // one queued resume plus one drainer; reserved internal slots cannot be
    // consumed by new external admission. Never block an RPC reaction thread.
    pending_internal_jobs_.fetch_add(1,std::memory_order_acq_rel);
    const auto result=pool_->TrySubmit([this,fn=std::move(fn)]()mutable {
        try {fn();}catch(...){worker_exception_total_.fetch_add(1,std::memory_order_relaxed);}
        pending_internal_jobs_.fetch_sub(1,std::memory_order_acq_rel);
        {std::lock_guard lock(drain_mutex_);drain_cv_.notify_all();}
    });
    if(result!=TaskPushResult::kOk) {
        pending_internal_jobs_.fetch_sub(1,std::memory_order_acq_rel);
        {std::lock_guard lock(drain_mutex_);drain_cv_.notify_all();}
        throw std::logic_error("accepted continuation reserve invariant violated");
    }
}

void BusinessExecutor::FinishAsync(const std::shared_ptr<TaskState>& task,
                                 Completion completion,std::exception_ptr error) {
    if(task->async_finished.exchange(true,std::memory_order_acq_rel))return;
    async_finished_total_.fetch_add(1,std::memory_order_relaxed);
    if(error) {
        worker_exception_total_.fetch_add(1,std::memory_order_relaxed);
        ReleasePending();
    }else if(completion) {
        DispatchCompletion(task,std::move(completion));
    }else {
        completed_total_.fetch_add(1,std::memory_order_relaxed);
        ReleasePending();
    }
    // pending_async_tasks protects this scheduling tail against shutdown,
    // including an inline completion on the original starter worker.
    if(task->async_stripe) {
        if(task->async_key) {
            if(CompleteKeyTask(*task->async_stripe,*task->async_key))
                PostInternal([this,index=*task->async_stripe,key=*task->async_key]{DrainKey(index,key);});
        }else PostInternal([this,index=*task->async_stripe]{DrainStripe(index);});
    }
    pending_async_tasks_.fetch_sub(1,std::memory_order_acq_rel);
    {std::lock_guard lock(drain_mutex_);drain_cv_.notify_all();}
}

bool BusinessExecutor::CompletionStillValid(
    const std::shared_ptr<TaskState>& task
) noexcept {
    if (!task) {
        return false;
    }

    if (!task->spec.completion_still_valid) {
        return true;
    }

    try {
        return task->spec.completion_still_valid();
    } catch (const std::exception& e) {
        LOG_ERROR(
            "business completion validity probe failed"
            << ", operation="
            << task->spec.request.operation
            << ", user_id="
            << task->spec.request.user_id
            << ", request_seq="
            << task->spec.request.request_seq
            << ", error="
            << e.what()
        );
    } catch (...) {
        LOG_ERROR(
            "business completion validity probe failed"
            << ", operation="
            << task->spec.request.operation
            << ", user_id="
            << task->spec.request.user_id
            << ", request_seq="
            << task->spec.request.request_seq
            << ", error=unknown_exception"
        );
    }

    /*
     * 无法证明Completion仍然安全时fail closed。
     */
    return false;
}


void BusinessExecutor::DispatchDeadlineTerminal(
    const std::shared_ptr<TaskState>& task
) {
    if (
        !task ||
        !task->spec.
            deadline_expired_before_start_completion
    ) {
        ReleasePending();
        return;
    }

    if (!task->spec.dispatcher) {
        deadline_terminal_dropped_total_.fetch_add(
            1,
            std::memory_order_relaxed
        );
        ReleasePending();
        return;
    }

    /*
     * Deadline terminal response 仍必须尊重原 connection/session
     * completion fence。连接已经消失时不能发送陈旧响应。
     */
    if (!CompletionStillValid(task)) {
        deadline_terminal_dropped_total_.fetch_add(
            1,
            std::memory_order_relaxed
        );
        ReleasePending();
        return;
    }

    pending_completions_.fetch_add(
        1,
        std::memory_order_acq_rel
    );

    Completion completion =
        task->spec.
            deadline_expired_before_start_completion;

    Completion wrapped =
        [
            this,
            task,
            completion = std::move(completion)
        ]() mutable {
            if (!CompletionStillValid(task)) {
                deadline_terminal_dropped_total_.fetch_add(
                    1,
                    std::memory_order_relaxed
                );
                pending_completions_.fetch_sub(
                    1,
                    std::memory_order_acq_rel
                );
                ReleasePending();
                return;
            }

            try {
                completion();
                deadline_terminal_callback_total_.fetch_add(
                    1,
                    std::memory_order_relaxed
                );
            } catch (const std::exception& e) {
                deadline_terminal_exception_total_.fetch_add(
                    1,
                    std::memory_order_relaxed
                );
                LOG_ERROR(
                    "business deadline terminal completion failed"
                    << ", operation="
                    << task->spec.request.operation
                    << ", error="
                    << e.what()
                );
            } catch (...) {
                deadline_terminal_exception_total_.fetch_add(
                    1,
                    std::memory_order_relaxed
                );
                LOG_ERROR(
                    "business deadline terminal completion failed"
                    << ", operation="
                    << task->spec.request.operation
                    << ", error=unknown_exception"
                );
            }

            pending_completions_.fetch_sub(
                1,
                std::memory_order_acq_rel
            );
            ReleasePending();
        };

    try {
        task->spec.dispatcher(std::move(wrapped));
    } catch (const std::exception& e) {
        deadline_terminal_dropped_total_.fetch_add(
            1,
            std::memory_order_relaxed
        );
        pending_completions_.fetch_sub(
            1,
            std::memory_order_acq_rel
        );
        LOG_ERROR(
            "business deadline terminal dispatch failed"
            << ", operation="
            << task->spec.request.operation
            << ", error="
            << e.what()
        );
        ReleasePending();
    } catch (...) {
        deadline_terminal_dropped_total_.fetch_add(
            1,
            std::memory_order_relaxed
        );
        pending_completions_.fetch_sub(
            1,
            std::memory_order_acq_rel
        );
        LOG_ERROR(
            "business deadline terminal dispatch failed"
            << ", operation="
            << task->spec.request.operation
            << ", error=unknown_exception"
        );
        ReleasePending();
    }
}


void BusinessExecutor::DispatchCompletion(
    const std::shared_ptr<TaskState>& task,
    Completion completion
) {
    if (!completion) {
        completed_total_.fetch_add(
            1,
            std::memory_order_relaxed
        );
        ReleasePending();
        return;
    }

    if (!task || !task->spec.dispatcher) {
        completion_dropped_total_.fetch_add(
            1,
            std::memory_order_relaxed
        );
        ReleasePending();
        return;
    }

    /*
     * Completion Fence #1:
     * Worker已经结束，但尚未把callback交给EventLoop/dispatcher。
     */
    if (!CompletionStillValid(task)) {
        completion_dropped_total_.fetch_add(
            1,
            std::memory_order_relaxed
        );
        ReleasePending();
        return;
    }

    pending_completions_.fetch_add(
        1,
        std::memory_order_acq_rel
    );

    Completion wrapped_completion =
        [
            this,
            task,
            completion = std::move(completion)
        ]() mutable {
            /*
             * Completion Fence #2:
             * callback真正执行前再次确认logical Session ownership。
             */
            if (!CompletionStillValid(task)) {
                completion_dropped_total_.fetch_add(
                    1,
                    std::memory_order_relaxed
                );
                pending_completions_.fetch_sub(
                    1,
                    std::memory_order_acq_rel
                );
                ReleasePending();
                return;
            }

            bool success = true;

            try {
                completion();
            } catch (const std::exception& e) {
                success = false;
                completion_exception_total_.fetch_add(
                    1,
                    std::memory_order_relaxed
                );
                LOG_ERROR(
                    "business completion failed"
                    << ", operation="
                    << task->spec.request.operation
                    << ", error="
                    << e.what()
                );
            } catch (...) {
                success = false;
                completion_exception_total_.fetch_add(
                    1,
                    std::memory_order_relaxed
                );
                LOG_ERROR(
                    "business completion failed"
                    << ", operation="
                    << task->spec.request.operation
                    << ", error=unknown_exception"
                );
            }

            if (success) {
                completed_total_.fetch_add(
                    1,
                    std::memory_order_relaxed
                );
            }

            pending_completions_.fetch_sub(
                1,
                std::memory_order_acq_rel
            );
            ReleasePending();
        };

    try {
        task->spec.dispatcher(
            std::move(wrapped_completion)
        );
    } catch (const std::exception& e) {
        completion_dropped_total_.fetch_add(
            1,
            std::memory_order_relaxed
        );
        pending_completions_.fetch_sub(
            1,
            std::memory_order_acq_rel
        );
        LOG_ERROR(
            "business completion dispatch failed"
            << ", operation="
            << task->spec.request.operation
            << ", error="
            << e.what()
        );
        ReleasePending();
    } catch (...) {
        completion_dropped_total_.fetch_add(
            1,
            std::memory_order_relaxed
        );
        pending_completions_.fetch_sub(
            1,
            std::memory_order_acq_rel
        );
        LOG_ERROR(
            "business completion dispatch failed"
            << ", operation="
            << task->spec.request.operation
            << ", error=unknown_exception"
        );
        ReleasePending();
    }
}

const char* BusinessSubmitStatusToString(
    BusinessSubmitStatus status
) noexcept {
    switch (status) {
        case BusinessSubmitStatus::kAccepted:
            return "accepted";

        case BusinessSubmitStatus::kOverloaded:
            return "overloaded";

        case BusinessSubmitStatus::kHotKeyOverloaded:
            return "hot_key_overloaded";

        case BusinessSubmitStatus::kDeadlineExpired:
            return "deadline_expired";

        case BusinessSubmitStatus::kShuttingDown:
            return "shutting_down";

        case BusinessSubmitStatus::kInvalidArgument:
            return "invalid_argument";
    }

    return "unknown";
}

void BusinessExecutor::UpdatePeak(
    std::atomic<std::size_t>& peak,
    std::size_t value
) {
    std::size_t current =
        peak.load(
            std::memory_order_relaxed
        );

    while (
        value > current &&
        !peak.compare_exchange_weak(
            current,
            value,
            std::memory_order_relaxed
        )
    ) {
    }
}

void BusinessExecutor::UpdatePeakNs(
    std::atomic<std::uint64_t>& peak,
    std::uint64_t value
) {
    std::uint64_t current =
        peak.load(
            std::memory_order_relaxed
        );

    while (
        value > current &&
        !peak.compare_exchange_weak(
            current,
            value,
            std::memory_order_relaxed
        )
    ) {
    }
}

void BusinessExecutor::RecordQueueWait(
    std::chrono::nanoseconds elapsed
) {
    if (elapsed.count() < 0) {
        return;
    }

    const auto value =
        static_cast<std::uint64_t>(
            elapsed.count()
        );

    total_queue_wait_ns_.fetch_add(
        value,
        std::memory_order_relaxed
    );

    queue_wait_sample_count_.fetch_add(
        1,
        std::memory_order_relaxed
    );

    UpdatePeakNs(
        max_queue_wait_ns_,
        value
    );
}

void BusinessExecutor::RecordExecution(
    std::chrono::nanoseconds elapsed
) {
    if (elapsed.count() < 0) {
        return;
    }

    const auto value =
        static_cast<std::uint64_t>(
            elapsed.count()
        );

    total_execution_ns_.fetch_add(
        value,
        std::memory_order_relaxed
    );

    execution_sample_count_.fetch_add(
        1,
        std::memory_order_relaxed
    );

    UpdatePeakNs(
        max_execution_ns_,
        value
    );
}

bool BusinessExecutor::ReservePending() {
    std::size_t current =
        current_pending_tasks_.load(
            std::memory_order_relaxed
        );

    while (true) {
        if (
            current >=
            options_.max_pending_tasks
        ) {
            return false;
        }

        if (
            current_pending_tasks_.
                compare_exchange_weak(
                    current,
                    current + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed
                )
        ) {
            UpdatePeak(
                peak_pending_tasks_,
                current + 1
            );

            return true;
        }

        /*
         * compare_exchange_weak失败时，
         * current会自动被更新为最新值，
         * 下一轮继续检查capacity。
         */
    }
}


void BusinessExecutor::ReleasePending() {
    const std::size_t previous =
        current_pending_tasks_.fetch_sub(
            1,
            std::memory_order_acq_rel
        );

    if (previous == 0) {
        /*
         * 理论上绝对不应该发生。
         *
         * 不在这里继续fetch避免unsigned underflow。
         */
        current_pending_tasks_.store(
            0,
            std::memory_order_relaxed
        );

        LOG_ERROR(
            "business executor pending "
            "counter underflow"
        );

        return;
    }

    NotifyDrainProgress();
}

void BusinessExecutor::NotifyDrainProgress() {
    std::lock_guard lock(drain_mutex_);
    drain_cv_.notify_all();
}

}  // namespace tinyimx
