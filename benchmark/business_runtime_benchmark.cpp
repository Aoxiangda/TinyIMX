#include "gateway/business/BusinessExecutor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

struct Options {
    std::size_t workers{4};
    std::size_t tasks{256};
    std::uint64_t work_ms{10};
    std::size_t max_pending{512};
    std::size_t stripes{64};
    std::size_t per_stripe{128};
    std::string ordering{"unordered"};
    std::size_t keys{64};
    std::size_t submitters{1};
    std::string case_name{"manual"};
    std::string csv_path;
};

struct TaskTiming {
    std::int64_t submit_begin_ns{0};
    std::int64_t submit_end_ns{0};
    std::int64_t work_start_ns{0};
    std::int64_t work_end_ns{0};
    bool accepted{false};
};

struct LatencySummary {
    double p50_ms{0.0};
    double p95_ms{0.0};
    double p99_ms{0.0};
    double max_ms{0.0};
};

struct StatusCounts {
    std::uint64_t accepted{0};
    std::uint64_t overload{0};
    std::uint64_t hot_key{0};
    std::uint64_t deadline{0};
    std::uint64_t shutdown{0};
    std::uint64_t invalid{0};
};

std::int64_t ToNs(
    TimePoint value,
    TimePoint origin
) {
    return std::chrono::duration_cast<
        std::chrono::nanoseconds
    >(value - origin).count();
}

double NsToMs(std::int64_t ns) {
    return static_cast<double>(ns) / 1000000.0;
}

bool ParseSize(
    const std::string& text,
    std::size_t* output
) {
    if (output == nullptr || text.empty()) {
        return false;
    }

    try {
        std::size_t parsed = 0;
        const unsigned long long value =
            std::stoull(text, &parsed, 10);

        if (
            parsed != text.size() ||
            value >
                static_cast<unsigned long long>(
                    std::numeric_limits<std::size_t>::max()
                )
        ) {
            return false;
        }

        *output = static_cast<std::size_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseUInt64(
    const std::string& text,
    std::uint64_t* output
) {
    if (output == nullptr || text.empty()) {
        return false;
    }

    try {
        std::size_t parsed = 0;
        const unsigned long long value =
            std::stoull(text, &parsed, 10);

        if (parsed != text.size()) {
            return false;
        }

        *output = static_cast<std::uint64_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

void PrintUsage(const char* program) {
    std::cout
        << "Usage: "
        << (program != nullptr ? program : "business_runtime_benchmark")
        << " [options]\n\n"
        << "Options:\n"
        << "  --workers N\n"
        << "  --tasks N\n"
        << "  --work-ms N\n"
        << "  --max-pending N\n"
        << "  --stripes N\n"
        << "  --per-stripe N\n"
        << "  --ordering unordered|striped\n"
        << "  --keys N\n"
        << "  --submitters N\n"
        << "  --case NAME\n"
        << "  --csv PATH\n"
        << "  --help\n";
}

bool ParseArgs(
    int argc,
    char* argv[],
    Options* options
) {
    if (options == nullptr) {
        return false;
    }

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];

        if (arg == "--help") {
            PrintUsage(argv[0]);
            return false;
        }

        if (index + 1 >= argc) {
            std::cerr
                << "missing value for argument: "
                << arg
                << '\n';
            return false;
        }

        const std::string value = argv[++index];

        if (arg == "--workers") {
            if (!ParseSize(value, &options->workers)) {
                return false;
            }
        } else if (arg == "--tasks") {
            if (!ParseSize(value, &options->tasks)) {
                return false;
            }
        } else if (arg == "--work-ms") {
            if (!ParseUInt64(value, &options->work_ms)) {
                return false;
            }
        } else if (arg == "--max-pending") {
            if (!ParseSize(value, &options->max_pending)) {
                return false;
            }
        } else if (arg == "--stripes") {
            if (!ParseSize(value, &options->stripes)) {
                return false;
            }
        } else if (arg == "--per-stripe") {
            if (!ParseSize(value, &options->per_stripe)) {
                return false;
            }
        } else if (arg == "--ordering") {
            options->ordering = value;
        } else if (arg == "--keys") {
            if (!ParseSize(value, &options->keys)) {
                return false;
            }
        } else if (arg == "--submitters") {
            if (!ParseSize(value, &options->submitters)) {
                return false;
            }
        } else if (arg == "--case") {
            options->case_name = value;
        } else if (arg == "--csv") {
            options->csv_path = value;
        } else {
            std::cerr
                << "unknown argument: "
                << arg
                << '\n';
            return false;
        }
    }

    if (
        options->workers == 0 ||
        options->tasks == 0 ||
        options->max_pending == 0 ||
        options->stripes == 0 ||
        options->per_stripe == 0 ||
        options->submitters == 0 ||
        options->per_stripe > options->max_pending
    ) {
        std::cerr
            << "workers/tasks/max-pending/stripes/per-stripe/submitters "
               "must be positive and per-stripe must not exceed max-pending\n";
        return false;
    }

    if (
        options->ordering != "unordered" &&
        options->ordering != "striped"
    ) {
        std::cerr
            << "--ordering must be unordered or striped\n";
        return false;
    }

    if (
        options->ordering == "striped" &&
        options->keys == 0
    ) {
        std::cerr
            << "--keys must be greater than 0 for striped ordering\n";
        return false;
    }

    if (options->case_name.find(',') != std::string::npos) {
        std::cerr
            << "--case must not contain commas when CSV output is enabled\n";
        return false;
    }

    return true;
}

double Percentile(
    const std::vector<double>& sorted,
    double percentile
) {
    if (sorted.empty()) {
        return 0.0;
    }

    const double rank =
        std::ceil(
            percentile *
            static_cast<double>(sorted.size())
        );

    std::size_t index = 0;

    if (rank > 1.0) {
        index = static_cast<std::size_t>(rank - 1.0);
    }

    if (index >= sorted.size()) {
        index = sorted.size() - 1;
    }

    return sorted[index];
}

LatencySummary Summarize(
    std::vector<double> samples
) {
    LatencySummary summary;

    if (samples.empty()) {
        return summary;
    }

    std::sort(samples.begin(), samples.end());

    summary.p50_ms = Percentile(samples, 0.50);
    summary.p95_ms = Percentile(samples, 0.95);
    summary.p99_ms = Percentile(samples, 0.99);
    summary.max_ms = samples.back();

    return summary;
}

void CountStatus(
    tinyimx::BusinessSubmitStatus status,
    StatusCounts* counts
) {
    if (counts == nullptr) {
        return;
    }

    switch (status) {
        case tinyimx::BusinessSubmitStatus::kAccepted:
            ++counts->accepted;
            break;
        case tinyimx::BusinessSubmitStatus::kOverloaded:
            ++counts->overload;
            break;
        case tinyimx::BusinessSubmitStatus::kHotKeyOverloaded:
            ++counts->hot_key;
            break;
        case tinyimx::BusinessSubmitStatus::kDeadlineExpired:
            ++counts->deadline;
            break;
        case tinyimx::BusinessSubmitStatus::kShuttingDown:
            ++counts->shutdown;
            break;
        case tinyimx::BusinessSubmitStatus::kInvalidArgument:
            ++counts->invalid;
            break;
    }
}

bool IsFileEmpty(const std::string& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);

    if (!input.is_open()) {
        return true;
    }

    return input.tellg() <= 0;
}

bool AppendCsv(
    const Options& options,
    const StatusCounts& statuses,
    const tinyimx::BusinessExecutorStats& stats,
    bool within_budget,
    double elapsed_ms,
    double throughput,
    const LatencySummary& submit,
    const LatencySummary& queue,
    const LatencySummary& e2e
) {
    if (options.csv_path.empty()) {
        return true;
    }

    const bool write_header =
        IsFileEmpty(options.csv_path);

    std::ofstream output(
        options.csv_path,
        std::ios::out | std::ios::app
    );

    if (!output.is_open()) {
        std::cerr
            << "failed to open CSV: "
            << options.csv_path
            << '\n';
        return false;
    }

    if (write_header) {
        output
            << "case,workers,tasks,work_ms,max_pending,stripes,per_stripe,"
               "ordering,keys,submitters,submitted,accepted,completed,"
               "rejected_overload,rejected_hot_key,rejected_deadline,"
               "rejected_shutdown,rejected_invalid,elapsed_ms,throughput,"
               "submit_p50_ms,submit_p95_ms,submit_p99_ms,submit_max_ms,"
               "queue_p50_ms,queue_p95_ms,queue_p99_ms,"
               "e2e_p50_ms,e2e_p95_ms,e2e_p99_ms,"
               "peak_pending,peak_active,runtime_avg_queue_wait_ms,"
               "runtime_max_queue_wait_ms,runtime_avg_execution_ms,"
               "runtime_max_execution_ms,within_shutdown_budget\n";
    }

    output
        << std::fixed
        << std::setprecision(6)
        << options.case_name << ','
        << options.workers << ','
        << options.tasks << ','
        << options.work_ms << ','
        << options.max_pending << ','
        << options.stripes << ','
        << options.per_stripe << ','
        << options.ordering << ','
        << (options.ordering == "striped" ? options.keys : 0) << ','
        << options.submitters << ','
        << stats.submitted_total << ','
        << statuses.accepted << ','
        << stats.completed_total << ','
        << statuses.overload << ','
        << statuses.hot_key << ','
        << statuses.deadline << ','
        << statuses.shutdown << ','
        << statuses.invalid << ','
        << elapsed_ms << ','
        << throughput << ','
        << submit.p50_ms << ','
        << submit.p95_ms << ','
        << submit.p99_ms << ','
        << submit.max_ms << ','
        << queue.p50_ms << ','
        << queue.p95_ms << ','
        << queue.p99_ms << ','
        << e2e.p50_ms << ','
        << e2e.p95_ms << ','
        << e2e.p99_ms << ','
        << stats.peak_pending_tasks << ','
        << stats.peak_active_tasks << ','
        << stats.average_queue_wait_ms << ','
        << stats.max_queue_wait_ms << ','
        << stats.average_execution_ms << ','
        << stats.max_execution_ms << ','
        << (within_budget ? 1 : 0)
        << '\n';

    return static_cast<bool>(output);
}

}  // namespace

int main(int argc, char* argv[]) {
    Options options;

    if (!ParseArgs(argc, argv, &options)) {
        if (
            argc <= 1 ||
            (argc > 1 && std::string(argv[1]) != "--help")
        ) {
            PrintUsage(argv[0]);
            return 2;
        }

        return 0;
    }

    tinyimx::BusinessExecutorOptions runtime_options;
    runtime_options.worker_threads = options.workers;
    runtime_options.max_pending_tasks = options.max_pending;
    runtime_options.stripe_count = options.stripes;
    runtime_options.per_stripe_queue_capacity = options.per_stripe;

    /*
     * Capacity benchmark must not accidentally become a deadline test.
     * M13-C2 owns deadline/fault acceptance, so C1 uses a deliberately
     * generous budget and kMustRun work.
     */
    runtime_options.default_deadline =
        std::chrono::minutes(30);
    runtime_options.shutdown_timeout =
        std::chrono::minutes(5);

    tinyimx::BusinessExecutor executor(runtime_options);

    if (!executor.Start()) {
        std::cerr
            << "business executor start failed\n";
        return 1;
    }

    std::vector<TaskTiming> timings(options.tasks);
    std::vector<tinyimx::BusinessSubmitStatus>
        submit_statuses(
            options.tasks,
            tinyimx::BusinessSubmitStatus::kInvalidArgument
        );

    std::atomic<std::size_t> next_task{0};
    const TimePoint benchmark_origin = Clock::now();
    const TimePoint elapsed_begin = benchmark_origin;

    std::vector<std::thread> submitter_threads;
    submitter_threads.reserve(options.submitters);

    for (
        std::size_t submitter_index = 0;
        submitter_index < options.submitters;
        ++submitter_index
    ) {
        submitter_threads.emplace_back(
            [
                &executor,
                &options,
                &timings,
                &submit_statuses,
                &next_task,
                benchmark_origin
            ]() {
                while (true) {
                    const std::size_t task_id =
                        next_task.fetch_add(
                            1,
                            std::memory_order_relaxed
                        );

                    if (task_id >= options.tasks) {
                        break;
                    }

                    const TimePoint submit_begin = Clock::now();
                    timings[task_id].submit_begin_ns =
                        ToNs(
                            submit_begin,
                            benchmark_origin
                        );

                    tinyimx::BusinessExecutor::TaskSpec task;
                    task.request.operation =
                        "benchmark.business_runtime";
                    task.request.request_seq =
                        static_cast<std::uint32_t>(
                            task_id & 0xFFFFFFFFULL
                        );
                    task.request.received_at = submit_begin;

                    if (options.ordering == "striped") {
                        task.request.ordering_key =
                            static_cast<
                                tinyimx::BusinessOrderingKey
                            >(
                                task_id % options.keys
                            );
                    }

                    task.cancellation_policy =
                        tinyimx::BusinessCancellationPolicy::kMustRun;

                    task.work =
                        [
                            &timings,
                            task_id,
                            benchmark_origin,
                            work_ms = options.work_ms
                        ](
                            const tinyimx::BusinessExecutor::ExecutionContext&
                        ) -> tinyimx::BusinessExecutor::Completion {
                            const TimePoint work_start = Clock::now();
                            timings[task_id].work_start_ns =
                                ToNs(
                                    work_start,
                                    benchmark_origin
                                );

                            if (work_ms > 0) {
                                std::this_thread::sleep_for(
                                    std::chrono::milliseconds(
                                        work_ms
                                    )
                                );
                            }

                            const TimePoint work_end = Clock::now();
                            timings[task_id].work_end_ns =
                                ToNs(
                                    work_end,
                                    benchmark_origin
                                );

                            return {};
                        };

                    const tinyimx::BusinessSubmitStatus status =
                        executor.Submit(std::move(task));

                    const TimePoint submit_end = Clock::now();
                    timings[task_id].submit_end_ns =
                        ToNs(
                            submit_end,
                            benchmark_origin
                        );
                    timings[task_id].accepted =
                        status ==
                            tinyimx::BusinessSubmitStatus::kAccepted;
                    submit_statuses[task_id] = status;
                }
            }
        );
    }

    for (auto& thread : submitter_threads) {
        thread.join();
    }

    const bool within_budget =
        executor.ShutdownGraceful();
    const TimePoint elapsed_end = Clock::now();

    const tinyimx::BusinessExecutorStats stats =
        executor.GetStats();

    StatusCounts statuses;
    for (const auto status : submit_statuses) {
        CountStatus(status, &statuses);
    }

    std::vector<double> submit_samples;
    std::vector<double> queue_samples;
    std::vector<double> execution_samples;
    std::vector<double> e2e_samples;

    submit_samples.reserve(options.tasks);
    queue_samples.reserve(
        static_cast<std::size_t>(statuses.accepted)
    );
    execution_samples.reserve(
        static_cast<std::size_t>(statuses.accepted)
    );
    e2e_samples.reserve(
        static_cast<std::size_t>(statuses.accepted)
    );

    bool timing_invariant_ok = true;

    for (const auto& timing : timings) {
        if (
            timing.submit_end_ns >=
            timing.submit_begin_ns
        ) {
            submit_samples.push_back(
                NsToMs(
                    timing.submit_end_ns -
                    timing.submit_begin_ns
                )
            );
        } else {
            timing_invariant_ok = false;
        }

        if (!timing.accepted) {
            continue;
        }

        if (
            timing.work_start_ns <
                timing.submit_begin_ns ||
            timing.work_end_ns <
                timing.work_start_ns
        ) {
            timing_invariant_ok = false;
            continue;
        }

        queue_samples.push_back(
            NsToMs(
                timing.work_start_ns -
                timing.submit_begin_ns
            )
        );

        execution_samples.push_back(
            NsToMs(
                timing.work_end_ns -
                timing.work_start_ns
            )
        );

        e2e_samples.push_back(
            NsToMs(
                timing.work_end_ns -
                timing.submit_begin_ns
            )
        );
    }

    const LatencySummary submit_summary =
        Summarize(std::move(submit_samples));
    const LatencySummary queue_summary =
        Summarize(std::move(queue_samples));
    const LatencySummary execution_summary =
        Summarize(std::move(execution_samples));
    const LatencySummary e2e_summary =
        Summarize(std::move(e2e_samples));

    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(
            elapsed_end - elapsed_begin
        ).count();

    const double throughput =
        elapsed_ms > 0.0
            ? static_cast<double>(stats.completed_total) /
                (elapsed_ms / 1000.0)
            : 0.0;

    std::cout
        << std::fixed
        << std::setprecision(3)
        << "========== TinyIMX Business Runtime Benchmark ==========\n"
        << "case                     = " << options.case_name << '\n'
        << "workers                  = " << options.workers << '\n'
        << "tasks                    = " << options.tasks << '\n'
        << "work_ms                  = " << options.work_ms << '\n'
        << "max_pending              = " << options.max_pending << '\n'
        << "stripes                  = " << options.stripes << '\n'
        << "per_stripe               = " << options.per_stripe << '\n'
        << "ordering                 = " << options.ordering << '\n'
        << "keys                     = "
        << (options.ordering == "striped" ? options.keys : 0) << '\n'
        << "submitters                = " << options.submitters << '\n'
        << '\n'
        << "submitted                = " << stats.submitted_total << '\n'
        << "accepted                 = " << statuses.accepted << '\n'
        << "completed                = " << stats.completed_total << '\n'
        << "rejected_overload        = " << statuses.overload << '\n'
        << "rejected_hot_key         = " << statuses.hot_key << '\n'
        << "rejected_deadline        = " << statuses.deadline << '\n'
        << "rejected_shutdown        = " << statuses.shutdown << '\n'
        << "rejected_invalid         = " << statuses.invalid << '\n'
        << '\n'
        << "elapsed_ms               = " << elapsed_ms << '\n'
        << "throughput_tasks_per_sec = " << throughput << '\n'
        << '\n'
        << "submit_latency_ms:\n"
        << "  p50 = " << submit_summary.p50_ms << '\n'
        << "  p95 = " << submit_summary.p95_ms << '\n'
        << "  p99 = " << submit_summary.p99_ms << '\n'
        << "  max = " << submit_summary.max_ms << '\n'
        << '\n'
        << "admission_to_start_ms:\n"
        << "  p50 = " << queue_summary.p50_ms << '\n'
        << "  p95 = " << queue_summary.p95_ms << '\n'
        << "  p99 = " << queue_summary.p99_ms << '\n'
        << "  max = " << queue_summary.max_ms << '\n'
        << '\n'
        << "execution_ms:\n"
        << "  p50 = " << execution_summary.p50_ms << '\n'
        << "  p95 = " << execution_summary.p95_ms << '\n'
        << "  p99 = " << execution_summary.p99_ms << '\n'
        << "  max = " << execution_summary.max_ms << '\n'
        << '\n'
        << "end_to_end_ms:\n"
        << "  p50 = " << e2e_summary.p50_ms << '\n'
        << "  p95 = " << e2e_summary.p95_ms << '\n'
        << "  p99 = " << e2e_summary.p99_ms << '\n'
        << "  max = " << e2e_summary.max_ms << '\n'
        << '\n'
        << "runtime_internal:\n"
        << "  peak_pending       = " << stats.peak_pending_tasks << '\n'
        << "  peak_active        = " << stats.peak_active_tasks << '\n'
        << "  queue_wait_avg_ms  = " << stats.average_queue_wait_ms << '\n'
        << "  queue_wait_max_ms  = " << stats.max_queue_wait_ms << '\n'
        << "  execution_avg_ms   = " << stats.average_execution_ms << '\n'
        << "  execution_max_ms   = " << stats.max_execution_ms << '\n'
        << "  worker_exceptions  = " << stats.worker_exception_total << '\n'
        << "  completion_errors  = " << stats.completion_exception_total << '\n'
        << "  current_pending    = " << stats.current_pending_tasks << '\n'
        << "  pending_completion = " << stats.pending_completions << '\n'
        << "  within_shutdown_budget = " << (within_budget ? 1 : 0) << '\n'
        << "========================================================\n";

    const bool runtime_invariant_ok =
        stats.submitted_total == options.tasks &&
        statuses.accepted == stats.accepted_total &&
        stats.accepted_total == stats.completed_total &&
        stats.worker_exception_total == 0 &&
        stats.completion_exception_total == 0 &&
        stats.current_pending_tasks == 0 &&
        stats.pending_completions == 0;

    if (!timing_invariant_ok) {
        std::cerr
            << "[FAIL] benchmark timing invariant violated\n";
    }

    if (!runtime_invariant_ok) {
        std::cerr
            << "[FAIL] business runtime lifecycle invariant violated"
            << ", submitted=" << stats.submitted_total
            << ", accepted=" << stats.accepted_total
            << ", completed=" << stats.completed_total
            << ", worker_exceptions=" << stats.worker_exception_total
            << ", completion_exceptions=" << stats.completion_exception_total
            << ", current_pending=" << stats.current_pending_tasks
            << ", pending_completions=" << stats.pending_completions
            << '\n';
    }

    if (
        !AppendCsv(
            options,
            statuses,
            stats,
            within_budget,
            elapsed_ms,
            throughput,
            submit_summary,
            queue_summary,
            e2e_summary
        )
    ) {
        return 1;
    }

    return
        timing_invariant_ok &&
        runtime_invariant_ok
            ? 0
            : 1;
}
