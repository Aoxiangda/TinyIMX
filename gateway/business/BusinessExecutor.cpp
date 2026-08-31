#include "gateway/business/BusinessExecutor.h"

#include "common/logging/LogMacros.h"

#include <chrono>
#include <exception>
#include <utility>

namespace tinyimx {

struct BusinessExecutor::TaskState {
    TaskSpec spec;

    BusinessTimePoint enqueued_at{
        BusinessClock::now()
    };
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

    pool_options.name =
        "gateway-business-runtime";

    pool_options.worker_threads =
        options_.worker_threads;

    pool_options.queue_capacity =
        options_.max_pending_tasks;

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
        << ", workers="
        << options_.worker_threads
        << ", max_pending="
        << options_.max_pending_tasks
        << ", stripes="
        << options_.stripe_count
        << ", per_stripe_capacity="
        << options_.per_stripe_queue_capacity
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
    if (pool_) {
        pool_->Shutdown(
            ShutdownMode::kGraceful,
            options_.shutdown_timeout
        );
    }

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
                ) == 0;
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
    if (!task.work) {
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

    return
        BusinessSubmitStatus::
            kOverloaded;
}


BusinessSubmitStatus
BusinessExecutor::SubmitOrdered(
    std::shared_ptr<TaskState> task,
    BusinessOrderingKey key
) {
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


    return
        BusinessSubmitStatus::
            kOverloaded;
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
        ExecuteTask(task);
    }
}


void BusinessExecutor::ExecuteTask(
    const std::shared_ptr<TaskState>& task
) {
    if (!task) {
        return;
    }


    const auto started_at =
        BusinessClock::now();

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

            ReleasePending();

            return;
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

            return;
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
        return;
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

        return;
    }


    DispatchCompletion(
        task,
        std::move(completion)
    );
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
    drain_cv_.notify_all();
}

}  // namespace tinyimx